// GPL-3.0-or-later. An in-memory substitute for upstream's sysfs FileHandler.
// There is no file opening, chmod, mount, or frequency write in this adapter.
use std::{collections::BTreeMap, path::Path, sync::Mutex};
static REQUESTS: Mutex<BTreeMap<i32, i64>> = Mutex::new(BTreeMap::new());
#[derive(Debug)]
pub struct FileHandler;
impl FileHandler {
    pub fn new() -> Self { Self }
    pub fn write_with_workround<P: AsRef<Path>, S: AsRef<[u8]>>(&mut self, path: P, content: S) -> anyhow::Result<()> {
        let path = path.as_ref().to_str().ok_or_else(|| anyhow::anyhow!("invalid path"))?;
        let rest = path.strip_prefix("/sys/devices/system/cpu/cpufreq/policy")
            .ok_or_else(|| anyhow::anyhow!("unexpected output path"))?;
        let (policy, attr) = rest.split_once('/').ok_or_else(|| anyhow::anyhow!("invalid policy"))?;
        let policy: i32 = policy.parse()?;
        anyhow::ensure!((0..=255).contains(&policy), "invalid policy id");
        anyhow::ensure!(["scaling_min_freq", "scaling_max_freq"].contains(&attr), "unexpected output attribute");
        let value: i64 = std::str::from_utf8(content.as_ref())?.trim().parse()?;
        anyhow::ensure!((0..=10_000_000).contains(&value), "invalid frequency");
        // Only minimum requests cross the IPC boundary. Thermal ceilings belong to Android.
        if attr == "scaling_min_freq" { REQUESTS.lock().unwrap().insert(policy, value); }
        Ok(())
    }
}
pub fn snapshot() -> Vec<(i32, i64)> {
    REQUESTS.lock().unwrap().iter().map(|(k, v)| (*k, *v)).collect()
}
