use std::{
    fmt::format,
    io::{Read, Write},
    net::{TcpListener, TcpStream},
    sync::Arc, sync::Mutex
};

use std::process::Command;
use std::thread;

const CONTROL_PORT: u32 = 5000;
const WORKER_PORT: u32 = 5001;

fn control_thread_loop(port: u32, worker_pid: Arc<Mutex<i32>>) -> std::io::Result<()> {
    let address = format!("0.0.0.0:{}", port);
    let listener = TcpListener::bind(address)?;

    println!("Control listening on port {port}...");

    for stream in listener.incoming() {
        if let Ok(mut stream) = stream {
            let pid = worker_pid.lock().unwrap();
            let response = format!("testing, worker_pid {}\n", pid);
            let _ = stream.write_all(response.as_str().as_bytes());
        }
    } 
    Ok(())
}
 
fn worker_thread_loop(port: u32, worker_pid: Arc<Mutex<i32>>) -> std::io::Result<()> {
    let address = format!("0.0.0.0:{}", port);
    let listener = TcpListener::bind(address)?;

    println!("Worker listening on port {port}...");
 
    for stream in listener.incoming() {
        if let Ok(mut stream) = stream {
            Command::new("ls")
                .spawn()
                .expect("sh command failed to start");

            *worker_pid.lock().unwrap() = 1000;
        }
    }
    Ok(())
}
 
fn main() -> () {
    let worker_pid: Arc<Mutex<i32>> = Arc::new(Mutex::new(-1));
    let worker_pid_clone: Arc<Mutex<i32>> = Arc::clone(&worker_pid);

    let control_handle = thread::spawn(move || {
        if let Err(e) = control_thread_loop(CONTROL_PORT, worker_pid_clone) {
            eprintln!("Error in control thread: {:?}", e);
        }
    });

    let worker_handle = thread::spawn(move || {
        if let Err(e) = worker_thread_loop(WORKER_PORT, worker_pid) {
            eprintln!("Error in control thread: {:?}", e);
        }
    });

    control_handle.join().expect("Control thread panicked");
    worker_handle.join().expect("Worker thread panicked");

    loop {
        thread::park();
    }
}
