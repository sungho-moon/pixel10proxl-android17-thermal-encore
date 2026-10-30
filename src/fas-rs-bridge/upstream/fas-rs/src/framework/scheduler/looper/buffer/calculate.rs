// Copyright 2024-2025, shadow3aaa
//
// This file is part of fas-rs.
//
// fas-rs is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// fas-rs is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along
// with fas-rs. If not, see <https://www.gnu.org/licenses/>.

use std::time::{Duration, Instant};

use likely_stable::unlikely;
#[cfg(debug_assertions)]
use log::debug;

use super::Buffer;
use crate::{Extension, api::trigger_target_fps_change, framework::config::TargetFps};

const TARGET_SWITCH_DWELL: Duration = Duration::from_secs(2);
const TARGET_SWITCH_HYSTERESIS_FPS: f64 = 3.0;

impl Buffer {
    pub fn calculate_current_fps(&mut self) {
        let avg_time_long = self.calculate_average_frametime(None);
        #[cfg(debug_assertions)]
        debug!("avg_time_long: {avg_time_long:?}");

        self.frametime_state.avg_time_long = avg_time_long;

        let current_fps_long = 1.0 / avg_time_long.as_secs_f64();
        #[cfg(debug_assertions)]
        debug!("current_fps_long: {current_fps_long:.2}");

        self.frametime_state.current_fps_long = current_fps_long;

        let target_fps = self.target_fps().map(|target_fps| target_fps as usize);
        let avg_time_short = self.calculate_average_frametime(target_fps);
        #[cfg(debug_assertions)]
        debug!("avg_time_short: {avg_time_short:?}");

        self.frametime_state.avg_time_short = avg_time_short;

        let current_fps_short = 1.0 / avg_time_short.as_secs_f64();
        #[cfg(debug_assertions)]
        debug!("current_fps_short: {current_fps_short:.2}");

        self.frametime_state.current_fps_short = current_fps_short;
    }

    fn calculate_average_frametime(&self, it_takes: Option<usize>) -> Duration {
        let total_time: Duration = self
            .frametime_state
            .frametimes
            .iter()
            .take(it_takes.unwrap_or(self.frametime_state.frametimes.len()))
            .sum::<Duration>()
            .saturating_add(self.frametime_state.additional_frametime);

        total_time
            .checked_div(
                it_takes
                    .unwrap_or(self.frametime_state.frametimes.len())
                    .min(self.frametime_state.frametimes.len())
                    .try_into()
                    .unwrap(),
            )
            .unwrap_or_default()
    }

    pub fn calculate_target_fps(&mut self, extension: &Extension) {
        let new_target_fps = self.target_fps();
        if self.target_fps_state.target_fps != new_target_fps {
            if let Some(target_fps) = new_target_fps {
                self.trigger_target_fps_change(extension, target_fps);
                self.target_fps_state.target_fps = Some(target_fps);
                self.target_fps_state.pending_target_fps = None;
                self.target_fps_state.pending_target_since = None;
            }
        }
    }

    fn trigger_target_fps_change(&self, extension: &Extension, target_fps: u32) {
        trigger_target_fps_change(extension, target_fps, self.package_info.pkg.clone());
    }

    fn target_fps(&mut self) -> Option<u32> {
        let candidate = self.matched_target_fps();
        let Some(current) = self.target_fps_state.target_fps else {
            self.target_fps_state.pending_target_fps = None;
            self.target_fps_state.pending_target_since = None;
            return candidate;
        };

        let Some(candidate) = candidate else {
            // Keep the last selected target through brief dips below the
            // upstream matching floor, so the controller can still respond.
            self.target_fps_state.pending_target_fps = None;
            self.target_fps_state.pending_target_since = None;
            return Some(current);
        };

        if candidate == current {
            self.target_fps_state.pending_target_fps = None;
            self.target_fps_state.pending_target_since = None;
            return Some(current);
        }

        let fps = self.frametime_state.current_fps_long;
        // Add a deadband around the upstream +3 FPS matching edge and require
        // a stable candidate before changing the target.
        let switch_threshold = if candidate > current {
            f64::from(current) + 3.0 + TARGET_SWITCH_HYSTERESIS_FPS
        } else {
            f64::from(candidate) + 3.0 - TARGET_SWITCH_HYSTERESIS_FPS
        };
        let crossed = if candidate > current {
            fps >= switch_threshold
        } else {
            fps <= switch_threshold
        };

        if !crossed {
            self.target_fps_state.pending_target_fps = None;
            self.target_fps_state.pending_target_since = None;
            return Some(current);
        }

        if self.target_fps_state.pending_target_fps != Some(candidate) {
            self.target_fps_state.pending_target_fps = Some(candidate);
            self.target_fps_state.pending_target_since = Some(Instant::now());
            return Some(current);
        }

        if self.target_fps_state.pending_target_since
            .is_some_and(|since| since.elapsed() >= TARGET_SWITCH_DWELL)
        {
            Some(candidate)
        } else {
            Some(current)
        }
    }

    fn matched_target_fps(&self) -> Option<u32> {
        let target_fpses = match &self.target_fps_state.target_fps_config {
            TargetFps::Value(t) => vec![*t],
            TargetFps::Array(arr) => arr.clone(),
        };

        let current_fps = self.frametime_state.current_fps_long;

        if unlikely(current_fps < (target_fpses.first()?.saturating_sub(10).max(10)).into()) {
            return None;
        }

        for &target_fps in &target_fpses {
            if current_fps <= f64::from(target_fps) + 3.0 {
                #[cfg(debug_assertions)]
                debug!("Matched target_fps: current: {current_fps:.2} target_fps: {target_fps}");
                return Some(target_fps);
            }
        }

        target_fpses.last().copied()
    }
}

#[cfg(test)]
mod pixel_bridge_tests {
    use super::*;
    use super::super::BufferWorkingState;
    use crate::{Extension, framework::config::TargetFps};

    fn buffer() -> Buffer {
        Buffer::new(TargetFps::Array(vec![60, 90, 120]), 123, "game.test".into())
    }

    #[test]
    fn automatic_switch_uses_hysteresis_and_dwell() {
        let mut b = buffer();
        b.target_fps_state.target_fps = Some(90);

        b.frametime_state.current_fps_long = 95.0;
        assert_eq!(b.target_fps(), Some(90));
        assert_eq!(b.target_fps_state.pending_target_fps, None);

        b.frametime_state.current_fps_long = 97.0;
        assert_eq!(b.target_fps(), Some(90));
        assert_eq!(b.target_fps_state.pending_target_fps, Some(120));
        assert_eq!(b.target_fps(), Some(90));

        b.target_fps_state.pending_target_since =
            Some(Instant::now() - TARGET_SWITCH_DWELL - Duration::from_millis(1));
        b.calculate_target_fps(&Extension);
        assert_eq!(b.target_fps_state.target_fps, Some(120));

        b.frametime_state.current_fps_long = 92.0;
        assert_eq!(b.target_fps(), Some(120));
        assert_eq!(b.target_fps_state.pending_target_fps, None);
        b.frametime_state.current_fps_long = 89.0;
        assert_eq!(b.target_fps(), Some(120));
        assert_eq!(b.target_fps_state.pending_target_fps, Some(90));
        b.target_fps_state.pending_target_since =
            Some(Instant::now() - TARGET_SWITCH_DWELL - Duration::from_millis(1));
        b.calculate_target_fps(&Extension);
        assert_eq!(b.target_fps_state.target_fps, Some(90));
    }

    #[test]
    fn target_change_preserves_recent_frames_and_usable_state() {
        let mut b = buffer();
        b.target_fps_state.target_fps = Some(90);
        b.frametime_state.current_fps_long = 100.0;
        b.frametime_state
            .frametimes
            .extend(std::iter::repeat_n(Duration::from_millis(10), 120));
        b.state.working_state = BufferWorkingState::Usable;
        b.target_fps_state.pending_target_fps = Some(120);
        b.target_fps_state.pending_target_since =
            Some(Instant::now() - TARGET_SWITCH_DWELL - Duration::from_millis(1));

        b.calculate_target_fps(&Extension);

        assert_eq!(b.target_fps_state.target_fps, Some(120));
        assert_eq!(b.frametime_state.frametimes.len(), 120);
        assert_eq!(b.state.working_state, BufferWorkingState::Usable);
    }

    #[test]
    fn initial_unmatched_target_does_not_clear_frame_history() {
        let mut b = buffer();
        b.frametime_state
            .frametimes
            .extend(std::iter::repeat_n(Duration::from_millis(25), 20));
        b.frametime_state.current_fps_long = 40.0;

        b.calculate_target_fps(&Extension);
        b.calculate_target_fps(&Extension);

        assert_eq!(b.target_fps_state.target_fps, None);
        assert_eq!(b.frametime_state.frametimes.len(), 20);
    }
}
