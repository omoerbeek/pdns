use std::net::IpAddr;
use std::net::SocketAddr;
use std::str::FromStr;
use std::{error::Error, fmt};

use serde::{Deserialize, Serialize};

/* SECTION ONE: SIMPLE INTERFACE BETWEEN RUST AND C++ */

/* As an example, these functions are called from rec-main: rustTest() */

// modify a counter
#[no_mangle]
pub extern "C" fn rustIncrement(x: &mut i32) {
    *x += 1;
}

// modify a C string
#[no_mangle]
unsafe extern "C" fn rustString(str: *mut u8) {
    *str = b'g';
}

// print hello using the Rust runtime */
#[no_mangle]
extern "C" fn rustHello() {
    println!("Hello from Rust");
}

/* SECTION TWO INTERFACE BETWEEN RUST AND C++ USING CXX and SERDE */

/* The actual CXX/serde struct
This Struct has 3 section: Incoming, RecusorCache and DNSSEC */
#[cxx::bridge]
mod recconfig {
    // First section
    #[derive(Deserialize, Serialize, Debug, PartialEq)]
    pub struct Incoming {
        // Rust default value for bool is false, so we can use Default trait
        #[serde(default, skip_serializing_if = "crate::is_default")]
        pdns_distributes_queries: bool,

        // If we have a value with a default != Rust default, we have to do more work
        // The default value via a helper template
        // The test to skip via Bool helper if_true
        #[serde(
            default = "crate::Bool::<true>::v",
            skip_serializing_if = "crate::if_true"
        )]
        reuse_port: bool,

        // This one has a complex default value
        #[serde(default = "crate::default_listen", skip_serializing_if = "crate::default_listen_e")]
        listen: Vec<String>,

        #[serde(default, skip_serializing_if = "crate::is_default")]
        load_factor: f64,
    }

    // Second section
    #[derive(Deserialize, Serialize, Debug, PartialEq)]
    pub struct RecordCache {
        #[serde(
            default = "crate::U64::<1000000>::v",
            skip_serializing_if = "crate::U64::<1000000>::e"
        )]
        size: u64,
    }

    // Third section
    #[derive(Deserialize, Serialize, Debug, PartialEq)]
    pub struct DNSSEC {
        #[serde(
            default = "crate::U64::<100000>::v",
            skip_serializing_if = "crate::U64::<100000>::e"
        )]
        aggressive_cache_size: u64,
        #[serde(
            default = "crate::default_validation_mode",
            skip_serializing_if = "crate::default_validation_mode_e"
        )]
        validation: String,
    }

    // The complete config structure. Serializing a subsection is skipped if all its fields have default values
    #[derive(Deserialize, Serialize, Debug)]
    #[serde(deny_unknown_fields)]
    pub struct RecursorConfig {
        #[serde(default, skip_serializing_if = "crate::is_default")]
        incoming: Incoming,
        #[serde(default, skip_serializing_if = "crate::is_default")]
        record_cache: RecordCache,
        #[serde(default, skip_serializing_if = "crate::is_default")]
        dnssec: DNSSEC,
    }
    extern "Rust" {
        fn get_config_from_rust() -> RecursorConfig;
        fn get_config_from_rust2() -> RecursorConfig;
        fn get_config_from_rust3() -> RecursorConfig;
        fn pass_config_to_rust(config: &RecursorConfig);
    }
}

// Surpresses "Deserialize unused" warning
#[derive(Deserialize, Serialize)]
pub struct UnusedStruct {}

// GENERIC helpers

// A helper to define constant values and an equal function on them as a Rust path */
struct U64<const U: u64>;
impl<const U: u64> U64<U> {
    const fn v() -> u64 {
        U
    }
    fn e(v: &u64) -> bool {
        *v == U
    }
}

// A helper to define constant values and an equal function on them as a Rust path */
struct Bool<const U: bool>;
impl<const U: bool> Bool<U> {
    const fn v() -> bool {
        U
    }
}

// A helper used to decide if a bool value should be skipped
fn if_true(v: &bool) -> bool {
    *v
}

/* Helper code for validation of serde values */
#[derive(Debug)]
pub struct ValidationError {
    pub code: String,
}
impl Error for ValidationError {}
impl fmt::Display for ValidationError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.code)
    }
}
impl ValidationError {
    pub fn new(msg: String) -> ValidationError {
        ValidationError { code: msg }
    }
}

pub trait Validate {
    fn validate(&self) -> Result<(), ValidationError>;
}

/* Helper to decide if a value has a default value, as defined by Default trait */
static ALL: bool = false;
fn is_default<T: Default + PartialEq>(t: &T) -> bool {
    !ALL && t == &T::default()
}

// Incoming related helpers

// A function returning a complex value for a default */
pub fn default_listen() -> Vec<String> {
    [String::from("127.0.0.1")].to_vec()
}

pub fn default_listen_e(_v: &Vec<String>) -> bool {
    false // we're lazy
}

// Use the serde defaults to return a Struct with defaults
impl Default for recconfig::Incoming {
    fn default() -> Self {
        let deserialized: recconfig::Incoming = serde_yaml::from_str("").unwrap();
        deserialized
    }
}

impl Validate for recconfig::Incoming {
    fn validate(&self) -> Result<(), ValidationError> {
        for val in &self.listen {
            let sa = SocketAddr::from_str(&val);
            let ip = IpAddr::from_str(&val);
            if !sa.map_or(false, |i| i.is_ipv4() || i.is_ipv6())
                && !ip.map_or(false, |i| i.is_ipv4() || i.is_ipv6())
            {
                let msg = format!("value `{}' is not an IP or IP:port combination", val);
                return Err(ValidationError::new(msg));
            }
        }
        Ok(())
    }
}

// RecordCache releated helpers

// Use the serde defaulst to return a Struct with defaults
impl Default for recconfig::RecordCache {
    fn default() -> Self {
        let deserialized: recconfig::RecordCache = serde_yaml::from_str("").unwrap();
        deserialized
    }
}

// Use the serde defaults to return a Struct with defaults
impl Default for recconfig::RecursorConfig {
    fn default() -> Self {
        let deserialized: recconfig::RecursorConfig = serde_yaml::from_str("").unwrap();
        deserialized
    }
}

// DNSSEC related helpers

// Serde default cannot be literal, musty use a function
pub fn default_validation_mode() -> String {
    String::from("process")
}

// And a equal predicate
pub fn default_validation_mode_e(s: &String) -> bool {
    s.as_str() == default_validation_mode().as_str()
}

// Use the serde defaults to return a Struct with defaults
impl Default for recconfig::DNSSEC {
    fn default() -> Self {
        let deserialized: recconfig::DNSSEC = serde_yaml::from_str("").unwrap();
        deserialized
    }
}

// Below are a few test functions, to be called from C++
fn get_config_from_rust() -> recconfig::RecursorConfig {
    let config = recconfig::RecursorConfig {
        record_cache: recconfig::RecordCache { size: 1000 },
        dnssec: recconfig::DNSSEC {
            aggressive_cache_size: 99,
            validation: String::from("process"),
        },
        incoming: recconfig::Incoming {
            pdns_distributes_queries: false,
            reuse_port: true,
            listen: [String::from("127.0.0.1"), String::from("[::2]:53")].to_vec(),
            load_factor: 0.0,
        },
    };
    let serialized = serde_json::to_string(&config).unwrap();
    println!("JSON {}\n===", serialized);
    let deserialized: recconfig::RecursorConfig = serde_json::from_str(&serialized).unwrap();
    println!("JSON deserialzed {:?}\n===", deserialized);
    let serialized = serde_yaml::to_string(&config).unwrap();
    println!("YAML {}\n===", serialized);
    config.incoming.validate().unwrap();
    config
}

fn get_config_from_rust2() -> recconfig::RecursorConfig {
    let serialized = r###"{
    "record_cache" : {"size" : 1000 },
    "dnssec": {"aggressive_cache_size": 10000},
    "incoming" : {"pdns_distributes_queries":false}
  }"###;
    let deserialized: recconfig::RecursorConfig = serde_json::from_str(&serialized).unwrap();
    deserialized
}

fn get_config_from_rust3() -> recconfig::RecursorConfig {
    let serialized = r###"
  record_cache:
    size: 10000
  dnssec:
    validation: validate
  incoming:
    pdns_distributes_queries: false
  "###;
    let deserialized: recconfig::RecursorConfig = serde_yaml::from_str(&serialized).unwrap();
    deserialized
}

fn pass_config_to_rust(config: &recconfig::RecursorConfig) {
    let serialized = serde_yaml::to_string(&config).unwrap();
    println!("YAML received from C++\n{}\n===", serialized);
}
