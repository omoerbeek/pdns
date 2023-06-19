fn main() {
    cxx_build::bridge("src/lib.rs")
        .file("src/blobstore.cc")
        .flag_if_supported("-std=c++17")
        .compile("experiment");
}
