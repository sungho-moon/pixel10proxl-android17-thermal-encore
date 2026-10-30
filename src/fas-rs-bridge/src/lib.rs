// GPL-3.0-or-later. Integration glue; upstream algorithms retain their headers.
pub mod file_handler;
pub mod framework;
#[path = "../upstream/fas-rs/src/cpu_common/mod.rs"]
pub mod cpu_common;

// Extensions are deliberately absent: the bridge exports requests only.
pub struct Extension;
pub mod api {
    use crate::Extension;
    pub fn trigger_init_cpu_freq(_: &Extension) {}
    pub fn trigger_reset_cpu_freq(_: &Extension) {}
    pub fn trigger_target_fps_change(_: &Extension, _: u32, _: String) {}
}

pub use cpu_common::Controller;
pub use framework::scheduler::looper::buffer;

pub fn target_candidates(package: &str, fixed: u32) -> anyhow::Result<Vec<u32>> {
    if fixed != 0 {
        anyhow::ensure!([30, 60, 90, 120].contains(&fixed), "unsupported explicit target");
        return Ok(vec![fixed]);
    }
    let targets: serde_json::Value = serde_json::from_str(include_str!(
        "../../encore-fas/module/fas-rs-targets.json"
    ))?;
    let rates = targets.get(package).and_then(serde_json::Value::as_array)
        .map(|a| a.iter().filter_map(|v| v.as_u64().map(|v| v as u32)).collect())
        .unwrap_or_else(|| vec![30, 45, 60, 90, 120]);
    Ok(rates)
}

pub fn process_start(pid: i32) -> anyhow::Result<u64> {
    let stat = std::fs::read_to_string(format!("/proc/{pid}/stat"))?;
    let end = stat.rfind(')').ok_or_else(|| anyhow::anyhow!("invalid proc stat"))?;
    Ok(stat[end + 1..].split_whitespace().nth(19)
        .ok_or_else(|| anyhow::anyhow!("missing process start time"))?.parse()?)
}

pub fn monotonic_ms() -> u64 {
    let mut t = libc::timespec { tv_sec: 0, tv_nsec: 0 };
    unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut t); }
    t.tv_sec as u64 * 1000 + t.tv_nsec as u64 / 1_000_000
}

#[cfg(test)]
mod tests {
    use super::*;
    use framework::config::TargetFps;
    fn matched(fps: f64, rates: Vec<u32>) -> Option<u32> {
        let mut b = buffer::Buffer::new(TargetFps::Array(rates), 123, "game.test".into());
        b.frametime_state.current_fps_long = fps;
        b.calculate_target_fps(&Extension);
        b.target_fps_state.target_fps
    }
    #[test]
    fn upstream_matching_is_not_nearest_tier() {
        assert_eq!(matched(100.0, vec![60, 90, 120]), Some(120));
        assert_eq!(matched(92.0, vec![60, 90, 120]), Some(90));
        assert_eq!(matched(49.0, vec![60, 90, 120]), None);
    }
    #[test]
    fn low_window_does_not_immediately_drop_auto_target() {
        let mut b = buffer::Buffer::new(TargetFps::Array(vec![60, 90, 120]), 123, "game.test".into());
        for (fps, expected) in [(118.0, 120), (60.0, 120), (119.0, 120)] {
            b.frametime_state.current_fps_long = fps;
            b.calculate_target_fps(&Extension);
            assert_eq!(b.target_fps_state.target_fps, Some(expected));
        }
    }
    #[test]
    fn fixed_target_is_preserved_during_a_moderate_deficit() {
        assert_eq!(matched(111.0, vec![120]), Some(120));
    }
    #[test]
    fn uses_upstream_game_candidates_and_validates_fixed_input() {
        assert_eq!(target_candidates("com.tencent.tmgp.pubgmhd", 0).unwrap(), vec![60, 90, 120]);
        assert_eq!(target_candidates("com.tencent.tmgp.pubgmhd", 120).unwrap(), vec![120]);
        assert!(target_candidates("com.tencent.tmgp.pubgmhd", 144).is_err());
    }
    #[test]
    fn exports_requests_without_creating_sysfs_files() {
        let mut io = file_handler::FileHandler::new();
        io.write_with_workround("/sys/devices/system/cpu/cpufreq/policy255/scaling_min_freq", "1234").unwrap();
        assert!(file_handler::snapshot().contains(&(255, 1234)));
        assert!(io.write_with_workround("/tmp/not-a-cpu-policy", "1").is_err());
    }
}
