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
  let client = ffi::new_blobstore_client();

  // Upload a blob.
  let chunks = vec![b"fearless".to_vec(), b"concurrency".to_vec()];
  let mut buf = MultiBuf { chunks, pos: 0 };
  let blobid = client.put(&mut buf);
  println!("blobid = {}", blobid);
}


/* TESTED

macOS 13.4      Apple Silicon         # brew install rust
OpenBSD Current amd64                 # pkg_add rust
OpenBSD Current aarch64               # pkg_add rust
Debian Bullseye amd64                 # apt install rustc, -ldl fix
FreeBSD 13.2    amd64                 # pkg install rust
*/

#[cxx::bridge]
mod ffi {
   extern "Rust" {
        type MultiBuf;

        fn next_chunk(buf: &mut MultiBuf) -> &[u8];
    }

    unsafe extern "C++" {
        include!("experiment/include/blobstore.h");

        type BlobstoreClient;

        fn new_blobstore_client() -> UniquePtr<BlobstoreClient>;
        fn put(&self, parts: &mut MultiBuf) -> u64;
    }
}

// An iterator over contiguous chunks of a discontiguous file object. Toy
// implementation uses a Vec<Vec<u8>> but in reality this might be iterating
// over some more complex Rust data structure like a rope, or maybe loading
// chunks lazily from somewhere.
pub struct MultiBuf {
    chunks: Vec<Vec<u8>>,
    pos: usize,
}

pub fn next_chunk(buf: &mut MultiBuf) -> &[u8] {
    let next = buf.chunks.get(buf.pos);
    buf.pos += 1;
    next.map_or(&[], Vec::as_slice)
}
