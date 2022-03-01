use cluster::ClusterCoordinator;
use std::fs::File;
use std::io::Read;
use std::process::exit;
use std::thread::sleep;
use std::time::Duration;
mod colorprint;

macro_rules! error_and_exit_app {
  ($msg:expr, $print_usage:expr) => {
    error_and_exit_internal_app($msg, $print_usage)
  };
  ($msg:expr) => {
    error_and_exit_internal_app($msg, false)
  };
}

fn error_and_exit_internal_app(msg: &str, print_usage: bool) -> ! {
  redln!("Error: {}", msg);
  if print_usage {
    usage();
  }
  exit(1);
}

fn usage() {
  let args: Vec<String> = std::env::args().collect();
  println!(
    "Usage:\n\t {} <file_to_process> <number_of_processes> <character_to_count>\n",
    args[0]
  );
  exit(1);
}

trait ReadnExt {
  fn readn(&mut self, bytes_to_read: u64) -> Vec<u8>;
}

impl<R> ReadnExt for R
where
  R: Read,
{
  fn readn(&mut self, bytes_to_read: u64) -> Vec<u8>
  where
    R: Read,
  {
    let mut buf = vec![];
    let mut chunk = self.take(bytes_to_read);
    let n = match chunk.read_to_end(&mut buf) {
      Err(_why) => error_and_exit_app!("Failed to read from file.", true),
      Ok(res) => res,
    };
    if bytes_to_read as usize != n {
      error_and_exit_app!("Not enought bytes to read from file.");
    }
    buf
  }
}

fn main() {
  let args: Vec<String> = std::env::args().collect();
  if args.len() < 4 {
    error_and_exit_app!("Invalid number of arguments.", true);
  }
  if args[3].len() != 1 {
    error_and_exit_app!("Invalid argument value for character to count.", true);
  }
  let character_to_count = args[3].chars().nth(0).unwrap();

  let file_name = args[1].to_string();
  let mut processors_quantity = match args[2].parse::<u64>() {
    Err(_why) => error_and_exit_app!("Invalid argument value for number of processes.", true),
    Ok(res) => res,
  };
  let file_size = match std::fs::metadata(&file_name) {
    Err(_why) => error_and_exit_app!("Failed to open file.", true),
    Ok(metadata) => metadata.len(),
  };
  if file_size < 2 {
    error_and_exit_app!("Too small file.");
  }
  if processors_quantity > (file_size >> 1) {
    yellowln!("Warning: Quantity of processes specified ({}) exceeds half of the amount of information in file ({}).\nThe actual number of processes will be reduced...", processors_quantity, file_size>>1);
    processors_quantity = file_size >> 1;
  }
  let block_size = file_size / processors_quantity;
  let last_block_size = file_size - block_size * (processors_quantity - 1);

  let mut file = match File::open(file_name) {
    Err(_why) => error_and_exit_app!("Failed to open file.", true),
    Ok(file) => file,
  };

  let program = "
  #include <stdio.h>

  int main() {
    unsigned long size;
    fread(&size, sizeof(unsigned long), 1, stdin);
    char data[size];
    fread(data, 1, size, stdin);
    char to_find = data[size-1];
    unsigned long cnt = 0;
    for(unsigned long i = 0; i < size-1; ++i) {
      if(data[i] == to_find)
        ++cnt;
    }
    fwrite(&cnt, sizeof(unsigned long), 1, stdout);
    return 0;
  }
  ";
  let mut coord = ClusterCoordinator::new(program, 65535);
  let mut tasks = Vec::new();
  for _ in 0..(processors_quantity - 1) {
    let mut buf = file.readn(block_size);
    buf.push(character_to_count as u8);
    tasks.push(coord.add_task(buf));
  }
  let mut buf = file.readn(last_block_size);
  buf.push(character_to_count as u8);
  tasks.push(coord.add_task(buf));

  let mut cnt = 0u64;
  let mut finished = 0u64;
  while finished != processors_quantity {
    if let Some(extracted) = coord.extract_computed() {
      {
        let mut percentage = finished * 100 / processors_quantity;
        print!("{}", "\r".repeat((percentage + 4) as usize));
        finished += extracted.len() as u64;
        percentage = finished * 100 / processors_quantity;
        cyan!(
          "{:3}% {}",
          percentage,
          "\u{2588}".repeat(percentage as usize)
        );
      }
      for task in extracted {
        let res: &[u8] = &task.result.unwrap();
        cnt += u64::from_le_bytes(match res.try_into() {
          Ok(res) => res,
          Err(_) => error_and_exit_app!("Error converting result to u64"),
        });
      }
    };
  }
  greenln!("\nResult is: {}", cnt);
  coord.terminate();
  sleep(Duration::from_micros(500));
}
