#![allow(incomplete_features)]
#![feature(inherent_associated_types)]
mod colorprint;
use std::io::Read;
use std::process::exit;
use std::sync::atomic::AtomicU64;
use std::sync::{
  atomic::{AtomicBool, AtomicUsize, Ordering},
  Arc, Mutex,
};

#[macro_export]
macro_rules! error_and_exit {
  ($msg:expr, $err:expr) => {
    error_and_exit_internal(&$msg.to_string(), &$err.to_string())
  };
}

#[allow(dead_code)]
fn error_and_exit_internal(msg: &String, err: &String) -> ! {
  redln!("Error: {}: {}", msg, err);
  exit(1);
}
type Uid = usize;
type Guid = usize;

pub struct Program {
  pub program: Box<dyn Fn(&mut Task)>,
}

unsafe impl Send for Program {}

pub struct Task {
  guid: Guid,
  id: Uid,
  pub data: Vec<u8>,
  pub result: Vec<u8>,
  pub program: Option<Program>,
}

impl Task {
  pub fn new(data: Vec<u8>, id: Uid, prog: Box<dyn Fn(&mut Task)>) -> Self {
    static TASK_ID: AtomicUsize = AtomicUsize::new(0);
    let guid = TASK_ID.fetch_add(1, Ordering::Relaxed);
    Self {
      guid: guid,
      id: id,
      data: data,
      result: Vec::new(),
      program: Some(Program { program: prog }),
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
  thread_handle: Option<std::thread::JoinHandle<()>>,
  is_terminated: Arc<AtomicBool>,
}

fn handle_client(tasks: Arc<TasksContainer>, is_terminated: Arc<AtomicBool>) {
  if is_terminated.load(Ordering::Relaxed) {
    return;
  }
  let mut task = match tasks.take_idle() {
    Some(val) => val,
    None => {
      return;
    }
  };
  std::thread::spawn(move || {
    if let Some(program) = task.program.take() {
      (program.program)(&mut task);
      task.program = Some(program);
      tasks.push_succeeded(task);
      return;
    }
    yellowln!("Task does not hava a program to execute!");
    tasks.push_idle(task);
  });
}

impl ClusterCoordinator {
  type Compute = &'static fn(&mut Task) -> ();
  pub fn new() -> ClusterCoordinator {
    let mut result = ClusterCoordinator {
      tasks: Arc::new(TasksContainer::new()),
      thread_handle: None,
      is_terminated: Arc::new(AtomicBool::new(false)),
    };
    let tasks = Arc::clone(&result.tasks);
    let is_terminated = Arc::clone(&result.is_terminated);
    result.thread_handle = Some(std::thread::spawn(move || {
      while !is_terminated.load(Ordering::Relaxed) {
        handle_client(Arc::clone(&tasks), Arc::clone(&is_terminated));
      }
    }));
    result
  }
  pub fn add_task(&mut self, task: Vec<u8>, prog: Box<dyn Fn(&mut Task)>) -> Uid {
    let uid = self.tasks.get_new_uid();
    let task = Task::new(task, uid, prog);
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

  let mut file = match std::fs::File::open(file_name) {
    Err(_why) => error_and_exit_app!("Failed to open file.", true),
    Ok(file) => file,
  };

  let results = Arc::new(AtomicU64::new(0));
  let res_loc = Arc::clone(&results);
  let computer = move |task: &mut Task| {
    let (haystack, needle) = (
      &task.data[..task.data.len() - 1],
      task.data[task.data.len() - 1],
    );
    let mut cnt = 0u64;
    for c in haystack.iter() {
      if *c == needle {
        cnt += 1;
      }
    }
    res_loc.fetch_add(cnt, Ordering::Relaxed);
    task.result = cnt.to_le_bytes().to_vec();
  };

  let mut coord = ClusterCoordinator::new();
  let mut tasks = Vec::new();
  for _ in 0..(processors_quantity - 1) {
    let mut buf = file.readn(block_size);
    buf.push(character_to_count as u8);
    tasks.push(coord.add_task(buf, Box::new(computer.clone())));
  }
  let mut buf = file.readn(last_block_size);
  buf.push(character_to_count as u8);
  tasks.push(coord.add_task(buf, Box::new(computer)));

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
    };
  }
  greenln!("\nResult is: {}", results.load(Ordering::SeqCst));
  coord.terminate();
}
