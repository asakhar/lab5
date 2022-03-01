mod colorprint;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::process::exit;
use std::sync::{
  atomic::{AtomicBool, AtomicUsize, Ordering},
  Arc, Mutex,
};
use std::time::Duration;

macro_rules! error_and_exit {
  ($msg:expr, $err:expr) => {
    error_and_exit_internal(&$msg.to_string(), &$err.to_string())
  };
}

fn error_and_exit_internal(msg: &String, err: &String) -> ! {
  redln!("Error: {}: {}", msg, err);
  exit(1);
}
type Uid = usize;
type Guid = usize;

pub struct Task {
  guid: Guid,
  id: Uid,
  pub result: Option<Vec<u8>>,
  pub data: Vec<u8>,
}

impl Task {
  pub fn new(data: Vec<u8>, id: Uid) -> Self {
    static TASK_ID: AtomicUsize = AtomicUsize::new(0);
    let guid = TASK_ID.fetch_add(1, Ordering::Relaxed);
    Self {
      guid: guid,
      id: id,
      result: None,
      data: data,
    }
  }
  pub fn get_guid(&self) -> Guid {
    self.guid
  }
  pub fn get_uid(&self) -> Uid {
    self.id
  }
}

struct TasksContainer {
  idle_tasks: Arc<Mutex<Vec<Task>>>,
  succeeded_tasks: Arc<Mutex<Option<Vec<Task>>>>,
  id_max: AtomicUsize,
}
impl TasksContainer {
  fn new() -> Self {
    Self {
      idle_tasks: Arc::new(Mutex::new(Vec::new())),
      succeeded_tasks: Arc::new(Mutex::new(None)),
      id_max: AtomicUsize::new(0),
    }
  }
  fn push_idle(&self, task: Task) {
    let mut idle_tasks = match self.idle_tasks.lock() {
      Err(why) => error_and_exit!("Error locking idle tasks", why),
      Ok(res) => res,
    };
    idle_tasks.push(task);
    // blueln!("Task {} pushed to idle.", idle_tasks.last().unwrap().id);
  }
  fn take_idle(&self) -> Option<Task> {
    let mut idle_tasks = match self.idle_tasks.lock() {
      Err(why) => error_and_exit!("Error locking idle tasks", why),
      Ok(res) => res,
    };
    // if let Some(res) = idle_tasks.last() {
    //   blueln!("Task {} taken from idle.", res.id)
    // };
    idle_tasks.pop()
  }
  fn push_succeeded(&self, task: Task) {
    let mut succeeded_tasks = match self.succeeded_tasks.lock() {
      Err(why) => error_and_exit!("Error locking succeeded tasks", why),
      Ok(res) => res,
    };
    if let None = *succeeded_tasks {
      *succeeded_tasks = Some(Vec::new());
    }
    succeeded_tasks.as_mut().unwrap().push(task);
    // blueln!(
    //   "Task {} pushed to succeeded.",
    //   succeeded_tasks.as_ref().unwrap().last().unwrap().id
    // );
  }
  fn take_succeeded(&self) -> Option<Vec<Task>> {
    let mut succeeded_tasks = match self.succeeded_tasks.lock() {
      Err(why) => error_and_exit!("Error locking succeeded tasks", why),
      Ok(res) => res,
    };
    // if let Some(tasks) = succeeded_tasks.as_ref() {
    //   for task in tasks {
    //     blueln!("Task {} taken from succeeded.", task.id);
    //   }
    // }
    succeeded_tasks.take()
  }
  fn get_new_uid(&self) -> Uid {
    self.id_max.fetch_add(1, Ordering::SeqCst)
  }
}

pub struct ClusterCoordinator {
  tasks: Arc<TasksContainer>,
  program: Arc<String>,
  thread_handle: Option<std::thread::JoinHandle<()>>,
  is_terminated: Arc<AtomicBool>,
}

enum HostCommand {
  Wait = 0,
  Execute = 1,
  Terminate = 2,
}

fn write_data(stream: &mut TcpStream, data: &[u8], program: &[u8]) -> Result<(), std::io::Error> {
  stream.write_all(&program.len().to_le_bytes())?;
  stream.write_all(program)?;
  stream.write_all(&data.len().to_le_bytes())?;
  stream.write_all(&data)?;
  Ok(())
}

macro_rules! sizeof {
  ($t:ty) => {
    std::mem::size_of::<$t>()
  };
}

fn read_results(stream: &mut TcpStream) -> Result<Vec<u8>, std::io::Error> {
  let mut size_buf = [0u8; sizeof!(usize)];
  stream.read_exact(&mut size_buf)?;
  let mut buf = vec![0u8; usize::from_le_bytes(size_buf)];
  stream.read_exact(&mut buf)?;
  Ok(buf)
}

fn handle_client(
  mut stream: TcpStream,
  tasks: Arc<TasksContainer>,
  program: Arc<String>,
  is_terminated: Arc<AtomicBool>,
) {
  if is_terminated.load(Ordering::Relaxed) {
    stream
      .write_all(&[HostCommand::Terminate as u8])
      .unwrap_or_default();
    return;
  }
  let mut task = match tasks.take_idle() {
    Some(val) => {
      stream
        .write_all(&[HostCommand::Execute as u8])
        .unwrap_or_default();
      val
    }
    None => {
      stream
        .write_all(&[HostCommand::Wait as u8])
        .unwrap_or_default();
      return;
    }
  };
  std::thread::spawn(move || {
    if let Err(why) = write_data(&mut stream, &task.data, program.as_bytes()) {
      redln!("Error writing data to host: {}", why);
      tasks.push_idle(task);
      return;
    };
    stream
      .set_read_timeout(Some(Duration::from_secs(120)))
      .unwrap_or_default();
    match read_results(&mut stream) {
      Err(why) => {
        redln!("Error reading result from host: {}", why);
        tasks.push_idle(task)
      }
      Ok(res) => {
        task.result = Some(res);
        tasks.push_succeeded(task)
      }
    };
  });
}

impl ClusterCoordinator {
  pub fn new(program: &str, port: u16) -> ClusterCoordinator {
    let mut result = ClusterCoordinator {
      tasks: Arc::new(TasksContainer::new()),
      program: Arc::new(program.to_string()),
      thread_handle: None,
      is_terminated: Arc::new(AtomicBool::new(false)),
    };
    let tasks = Arc::clone(&result.tasks);
    let program = Arc::clone(&result.program);
    let is_terminated = Arc::clone(&result.is_terminated);
    result.thread_handle = Some(std::thread::spawn(move || {
      let address = std::net::SocketAddrV4::new(std::net::Ipv4Addr::new(0, 0, 0, 0), port);
      let listener = match TcpListener::bind(address) {
        Err(why) => error_and_exit!("Failed to bind to port", why),
        Ok(res) => res,
      };
      for stream in listener.incoming() {
        match stream {
          Ok(stream) => {
            handle_client(
              stream,
              Arc::clone(&tasks),
              Arc::clone(&program),
              Arc::clone(&is_terminated),
            );
          }
          Err(why) => {
            redln!("Error: {}", why);
            if is_terminated.load(Ordering::Relaxed) {
              break;
            }
          }
        }
      }
    }));
    result
  }
  pub fn add_task(&mut self, task: Vec<u8>) -> Uid {
    let uid = self.tasks.get_new_uid();
    let task = Task::new(task, uid);
    self.tasks.push_idle(task);
    uid
  }
  pub fn extract_computed(&mut self) -> Option<Vec<Task>> {
    self.tasks.take_succeeded()
  }
  pub fn terminate(&self) {
    self.is_terminated.store(true, Ordering::Relaxed);
  }
}
