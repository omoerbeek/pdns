#[no_mangle]
pub extern "C" fn rustIncrement(x: &mut i32) {
  *x += 1;
}

#[no_mangle]
unsafe extern "C" fn rustString(str: *mut u8) {
  *str = b'g';
}

#[no_mangle]
extern "C" fn rustHello() {
  println!("Hello from Rust");
}


/* TESTED

macOS 13.4      Apple Silicon         # brew install rust
OpenBSD Current amd64                 # pkg_add rust
OpenBSD Current aarch64               # pkg_add rust
Debian Bullseye amd64                 # apt install rustc, -ldl fix
FreeBSD 13.2    amd64                 # pkg install rust
*/