fn main() {
    cxx_build::bridge("src/lib.rs")
        .file("src/empty.cc")
        .flag_if_supported("-std=c++17")
        .compile("experiment");
}
