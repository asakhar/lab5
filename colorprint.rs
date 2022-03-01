#[macro_export]
macro_rules! printcolor {
  ($color:expr, $($args:expr),*) => {
    {
      print!("\u{001b}[{}m", $color);
      print!($($args),*);
      print!("\u{001b}[0m");
    }
  };
}

#[macro_export]
macro_rules! black {
  ($($args:expr), *) => {
    printcolor!(30, $($args),*)
  };
}

#[macro_export]
macro_rules! red {
  ($($args:expr), *) => {
    printcolor!(31, $($args),*)
  };
}

#[macro_export]
macro_rules! green {
  ($($args:expr), *) => {
    printcolor!(32, $($args),*)
  };
}

#[macro_export]
macro_rules! yellow {
  ($($args:expr), *) => {
    printcolor!(33, $($args),*)
  };
}

#[macro_export]
macro_rules! blue {
  ($($args:expr), *) => {
    printcolor!(34, $($args),*)
  };
}

#[macro_export]
macro_rules! magenta {
  ($($args:expr), *) => {
    printcolor!(35, $($args),*)
  };
}

#[macro_export]
macro_rules! cyan {
  ($($args:expr), *) => {
    printcolor!(36, $($args),*)
  };
}

#[macro_export]
macro_rules! white {
  ($($args:expr), *) => {
    printcolor!(37, $($args),*)
  };
}

#[macro_export]
macro_rules! printlncolor {
  ($color:expr, $($args:expr),*) => {
    {
      print!("\u{001b}[{}m", $color);
      print!($($args),*);
      println!("\u{001b}[0m");
    }
  };
}

#[macro_export]
macro_rules! blackln {
  ($($args:expr), *) => {
    printlncolor!(30, $($args),*)
  };
}

#[macro_export]
macro_rules! redln {
  ($($args:expr), *) => {
    printlncolor!(31, $($args),*)
  };
}

#[macro_export]
macro_rules! greenln {
  ($($args:expr), *) => {
    printlncolor!(32, $($args),*)
  };
}

#[macro_export]
macro_rules! yellowln {
  ($($args:expr), *) => {
    printlncolor!(33, $($args),*)
  };
}

#[macro_export]
macro_rules! blueln {
  ($($args:expr), *) => {
    printlncolor!(34, $($args),*)
  };
}

#[macro_export]
macro_rules! magentaln {
  ($($args:expr), *) => {
    printlncolor!(35, $($args),*)
  };
}

#[macro_export]
macro_rules! cyanln {
  ($($args:expr), *) => {
    printlncolor!(36, $($args),*)
  };
}

#[macro_export]
macro_rules! whiteln {
  ($($args:expr), *) => {
    printlncolor!(37, $($args),*)
  };
}
