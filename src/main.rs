use std::{
    fmt::format,
    io::{Read, Write},
    net::{TcpListener, TcpStream},
};

const CONTROL_PORT: u32 = 5000;
const WORKER_PORT: u32 = 5001;

fn control_thread_loop(port: u32) -> std::io::Result<()> {
    let address = format!("0.0.0.0:{}", port);
    let listener = TcpListener::bind(address)?;

    let mut buf = Vec::with_capacity(128);

    for stream in listener.incoming() {
        if let Ok(mut stream) = stream {
            let _ = stream.write_all(b"testing\n");
            stream.read_to_end(&mut buf)?;
            for i in &buf {
                print!("{}", i);
            }
        }
    }
    Ok(())
}

fn main() {
    println!("Hello, world!");

    std::thread::spawn(|| {
        let _ = control_thread_loop(CONTROL_PORT);
    });

    let dur = std::time::Duration::from_secs(1000);
    std::thread::sleep(dur);
}
