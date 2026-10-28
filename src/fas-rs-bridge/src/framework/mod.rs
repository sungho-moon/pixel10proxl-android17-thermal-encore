pub mod scheduler;
pub mod config {
    use std::collections::HashMap;
    #[derive(Debug, Clone)]
    pub enum TargetFps { Value(u32), Array(Vec<u32>) }
    pub enum MarginFps { BaseOnly(f64), Advanced { base: f64, overrides: HashMap<String, f64> } }
    pub struct ModeConfig { pub margin_fps: MarginFps }
    pub struct Config { mode: ModeConfig }
    #[derive(Clone, Copy)]
    pub enum Mode { Balance }
    impl Config {
        pub fn new() -> Self { Self { mode: ModeConfig { margin_fps: MarginFps::BaseOnly(1.0) } } }
        pub fn mode_config(&mut self, _: Mode) -> &ModeConfig { &self.mode }
    }
}
pub mod prelude { pub use super::config::{Config, Mode}; }
