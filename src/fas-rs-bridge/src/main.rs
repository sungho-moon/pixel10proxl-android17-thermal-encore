#[cfg(feature = "analyzer")]
use std::{io::{self, Write}, time::{Duration, Instant}};
#[cfg(feature = "analyzer")]
use pixel_fas_rs_bridge::{*, framework::{config::{Config, Mode, TargetFps}, scheduler::looper::{ControllerState, policy}}};

#[cfg(feature = "analyzer")]
fn run() -> anyhow::Result<()> {
    use std::sync::{Arc, atomic::{AtomicBool, Ordering}};
    let args: Vec<_> = std::env::args().collect();
    if args.len() == 2 && args[1] == "--probe" {
        let library = std::ffi::CString::new("/system/lib64/libgui.so")?;
        let handle = unsafe { libc::dlopen(library.as_ptr(), libc::RTLD_NOW) };
        anyhow::ensure!(!handle.is_null(), "cannot load system libgui for probe");
        let mut analyzer = frame_analyzer::Analyzer::new()?;
        analyzer.attach_app(std::process::id() as i32)?;
        analyzer.detach_apps();
        unsafe { libc::dlclose(handle); }
        println!("FASRS_PROBE PASS eBPF=loaded libgui=resolved writes=none frames=not_tested");
        return Ok(());
    }
    anyhow::ensure!(args.len() == 5, "usage: pixel-fas-rs-bridge PID START_TICKS PACKAGE FIXED_FPS");
    let pid: i32 = args[1].parse()?;
    let start: u64 = args[2].parse()?;
    let fixed: u32 = args[4].parse()?;
    anyhow::ensure!(pid > 1 && process_start(pid)? == start, "process identity changed");
    let cmdline = std::fs::read(format!("/proc/{pid}/cmdline"))?;
    anyhow::ensure!(cmdline.split(|b| *b == 0).next() == Some(args[3].as_bytes()), "package identity mismatch");
    let running = Arc::new(AtomicBool::new(true));
    let flag = running.clone();
    ctrlc::set_handler(move || { flag.store(false, Ordering::Relaxed); })?;
    let mut analyzer = frame_analyzer::Analyzer::new()?;
    analyzer.attach_app(pid)?;
    let mut buffer = buffer::Buffer::new(TargetFps::Array(target_candidates(&args[3], fixed)?), pid, args[3].clone());
    let extension = Extension;
    let mut controller = Controller::new()?;
    controller.init_game(pid, &extension);
    let mut state = ControllerState { controller, params: policy::ControllerParams::default(), target_fps_offset: 0.0, usage_sample_timer: Instant::now() };
    let mut config = Config::new();
    let mut emit = Instant::now();
    let mut last_frame = Instant::now();
    let mut seq = 0_u64;
    let mut out = io::BufWriter::new(io::stdout().lock());
    while running.load(Ordering::Relaxed) {
        if process_start(pid).ok() != Some(start) { break; }
        if let Some((observed, interval)) = analyzer.recv_timeout(Duration::from_millis(100)) {
            if observed != pid { continue; }
            last_frame = Instant::now();
            buffer.push_frametime(interval, &extension);
        } else {
            buffer.additional_frametime(&extension);
        }
        let ready = buffer.state.working_state == buffer::BufferWorkingState::Usable
            && last_frame.elapsed() < Duration::from_millis(500)
            && buffer.frametime_state.frametimes.len() >= 60
            && buffer.target_fps_state.target_fps.is_some();
        if ready {
            if let Some((delta, janked)) = policy::controll::calculate_control(&buffer, &mut config, Mode::Balance, &mut state, 0.0) {
                state.controller.fas_update_freq(delta, janked);
            }
        }
        if emit.elapsed() >= Duration::from_millis(200) {
            emit = Instant::now(); seq += 1;
            let requests = if ready { file_handler::snapshot() } else { Vec::new() };
            let fps = buffer.frametime_state.current_fps_long;
            let milli_fps = if fps.is_finite() { (fps.clamp(0.0, 1000.0) * 1000.0) as u64 } else { 0 };
            write!(out, "FASRS1 {pid} {start} {} {seq} {} {milli_fps} {} {}", monotonic_ms(), buffer.target_fps_state.target_fps.unwrap_or(0), u8::from(ready), requests.len())?;
            for (policy, freq) in requests { write!(out, " {policy} {freq}")?; }
            writeln!(out)?; out.flush()?;
        }
    }
    Ok(())
}

fn main() {
    #[cfg(feature = "analyzer")]
    if let Err(e) = run() { eprintln!("FASRS_BRIDGE_FAILED: {e:#}"); std::process::exit(1); }
    #[cfg(not(feature = "analyzer"))]
    { eprintln!("this build has no frame analyzer; tests only"); std::process::exit(2); }
}
