noinst_LIBRARIES = librusttest.a
librusttest_a_SOURCES = src/lib.rs src/empty.cc

EXTRA_DIST = \
	Cargo.toml \
	build.rs

librusttest.a: src/lib.rs
	cargo build --release $(RUST_TARGET)
	cp target/release/librusttest.a librusttest.a
	cp target/cxxbridge/experiment/src/lib.rs.h .

clean-local:
	rm -rf target lib.rs.h
