#[path = "../../../../upstream/fas-rs/src/framework/scheduler/looper/buffer/mod.rs"]
pub mod buffer;
#[path = "../../../../upstream/fas-rs/src/framework/scheduler/looper/policy/mod.rs"]
pub mod policy;
pub struct ControllerState {
    pub controller: crate::Controller,
    pub params: policy::ControllerParams,
    pub target_fps_offset: f64,
    pub usage_sample_timer: std::time::Instant,
}
