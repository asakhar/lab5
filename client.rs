#![feature(unboxed_closures)]
#![feature(fn_traits)]
use std::collections::HashMap;
use std::convert::TryInto;
use std::fs::remove_file;
use std::fs::OpenOptions;
use std::io::{Error, ErrorKind, Read, Write};
use std::net::{Ipv4Addr, SocketAddrV4, TcpStream};
use std::process::{Command, Stdio};
mod colorprint;

#[allow(dead_code)]
enum HostCommand {
  Wait = 0,
  Execute = 1,
  Terminate = 2,
}

impl TryFrom<u8> for HostCommand {
  type Error = ();

  fn try_from(v: u8) -> Result<Self, Self::Error> {
    match v {
      x if x == HostCommand::Execute as u8 => Ok(HostCommand::Execute),
      x if x == HostCommand::Wait as u8 => Ok(HostCommand::Wait),
      x if x == HostCommand::Terminate as u8 => Ok(HostCommand::Terminate),
      _ => Err(()),
    }
  }
}

trait ReadSizedExt {
  fn read_sized(&mut self) -> Result<Vec<u8>, Error>;
}

trait WriteSizedExt {
  fn write_sized(&mut self, data: &Vec<u8>) -> Result<(), Error>;
}

impl ReadSizedExt for TcpStream {
  fn read_sized(&mut self) -> Result<Vec<u8>, Error> {
    let mut buf_size = [0u8; 8];
    self.read_exact(&mut buf_size)?;
    let mut buf = vec![0u8; usize::from_le_bytes(buf_size)];
    self.read_exact(&mut buf)?;
    Ok(buf)
  }
}

impl WriteSizedExt for TcpStream {
  fn write_sized(&mut self, data: &Vec<u8>) -> Result<(), Error> {
    self.write_all(&data.len().to_le_bytes())?;
    self.write_all(&data)?;
    Ok(())
  }
}

struct ClientReceiver {
  cache: HashMap<Vec<u8>, String>,
  address: SocketAddrV4,
}

impl ClientReceiver {
  fn new(address: &SocketAddrV4) -> Self {
    Self {
      cache: HashMap::new(),
      address: address.clone(),
    }
  }
  fn compile(&mut self, program: &Vec<u8>) -> Result<String, Error> {
    if let Some(res) = self.cache.get(program) {
      return Ok(res.clone());
    };
    static EXEC_BASE: &str = "./executable";
    let mut executable_name: String = EXEC_BASE.to_string();
    {
      let mut n = 0usize;
      while let Err(_why) = OpenOptions::new()
        .create_new(true)
        .write(true)
        .open(&executable_name)
      {
        executable_name = EXEC_BASE.to_string();
        executable_name.push_str(&n.to_string());
        n += 1;
      }
    }
    let mut child = match Command::new("gcc")
      .args(["-o", &executable_name, "-xc", "-"])
      .stdin(Stdio::piped())
      .spawn()
    {
      Err(why) => return Err(why),
      Ok(res) => res,
    };
    let mut stdin = child.stdin.take().unwrap();
    match stdin.write_all(&program) {
      Ok(()) => {
        drop(stdin);
      }
      Err(why) => return Err(why),
    };
    match match child.wait() {
      Ok(res) => res,
      Err(why) => return Err(why),
    }
    .code()
    {
      Some(0) => {}
      _ => {
        if let Err(why) = remove_file(executable_name) {
          redln!("Error happened during removing executable: {}", why);
        };
        return Err(Error::new(ErrorKind::Other, "Compilation failed"));
      }
    };
    self.cache.insert(program.clone(), executable_name.clone());
    Ok(executable_name)
  }

  fn execute(executable: &String, data: &Vec<u8>) -> Result<Vec<u8>, Error> {
    let mut child = Command::new(executable)
      .stdin(Stdio::piped())
      .stdout(Stdio::piped())
      .spawn()?;
    let stdin = child.stdin.as_mut().unwrap();
    stdin.write_all(&data)?;
    drop(stdin);
    let output = child.wait_with_output()?;
    return Ok(output.stdout);
  }
}

impl std::ops::FnOnce<()> for ClientReceiver {
  type Output = ();
  extern "rust-call" fn call_once(self, _: ()) -> Self::Output {}
}

impl std::ops::FnMut<()> for ClientReceiver {
  extern "rust-call" fn call_mut(&mut self, _: ()) -> Self::Output {
    loop {
      // std::thread::sleep(std::time::Duration::from_micros(500));
      if let Ok(mut stream) = TcpStream::connect(&self.address) {
        let mut host_command_buf = [0u8; 1];
        if let Err(why) = stream.read_exact(&mut host_command_buf) {
          redln!("Error happened during reading of host command: {}", why);
          continue;
        }
        let host_command = *host_command_buf.first().unwrap();
        match host_command.try_into() {
          Ok(HostCommand::Execute) => {}
          Ok(HostCommand::Wait) => {
            continue;
          }
          Ok(HostCommand::Terminate) => {
            break;
          }
          Err(()) => {
            redln!("Invalid HostCommand received!");
            continue;
          }
        };
        let program = match stream.read_sized() {
          Err(why) => {
            redln!("Error reading program from server: {}", why);
            continue;
          }
          Ok(res) => res,
        };
        let data = match stream.read_sized() {
          Err(why) => {
            redln!("Error reading data from server: {}", why);
            continue;
          }
          Ok(mut res) => {
            let mut dt = Vec::new();
            dt.append(&mut res.len().to_le_bytes().to_vec());
            dt.append(&mut res);
            dt
          }
        };
        let compiled = match self.compile(&program) {
          Err(why) => {
            eprintln!("Error during compilation stage: {}", why);
            continue;
          }
          Ok(res) => res,
        };
        let result = match Self::execute(&compiled, &data) {
          Ok(res) => res,
          Err(why) => {
            redln!(
              "Error during execution stage: {}. Removing file from cache...",
              why
            );
            self.cache.remove(&program);
            if let Err(why) = remove_file(compiled) {
              redln!("Error happened during removing executable: {}", why)
            };
            continue;
          }
        };
        if let Err(why) = stream.write_sized(&result) {
          redln!("Error writing to host: {}", why);
        }
      }
    }
  }
}

impl Drop for ClientReceiver {
  fn drop(&mut self) {
    for compiled in self.cache.values() {
      if let Err(why) = remove_file(compiled) {
        redln!("Error happened during removing executable: {}", why);
      };
    }
  }
}

fn main() {
  let mut recv: ClientReceiver =
    ClientReceiver::new(&SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 65535));
  recv();
  greenln!("Terminated.");
}
