use std::{env,fs,path::PathBuf};
fn main() {
 let source=env::var("PIXEL_FAS_BPF_OBJECT").expect("build eBPF first and set PIXEL_FAS_BPF_OBJECT");
 println!("cargo:rerun-if-env-changed=PIXEL_FAS_BPF_OBJECT");
 println!("cargo:rerun-if-changed={source}");
 let root=PathBuf::from(env::var("OUT_DIR").unwrap()).join("ebpf_target/bpfel-unknown-none");
 for mode in ["debug","release"] { let dir=root.join(mode);fs::create_dir_all(&dir).unwrap();fs::copy(&source,dir.join("frame-analyzer-ebpf")).unwrap(); }
}
