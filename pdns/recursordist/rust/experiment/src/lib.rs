use std::{error::Error, fmt};
use std::net::{SocketAddr};
use std::net::{IpAddr};
use std::str::FromStr;

use serde::{Deserialize, Serialize};

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

// Surpresses "Deserialize unused" warning
#[derive(Deserialize, Serialize)]
pub struct UnusedStruct {}

struct U64<const U: u64>;
impl<const U: u64> U64<U> {
    const fn v() -> u64 {
        U
    }
}

struct Bool<const U: bool>;
impl<const U: bool> Bool<U> {
    const fn v() -> bool {
        U
    }
}

#[derive(Debug)]
pub struct ValidationError {
    //pub code: Cow<'static, str>,
    pub code: String,
}
impl Error for ValidationError {}
impl fmt::Display for ValidationError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.code)
    }
}
impl ValidationError {
    //pub fn new(msg: &'static str) -> ValidationError {
    //    ValidationError{ code: Cow::from(msg) }
    //}
    pub fn new(msg: String) -> ValidationError {
        ValidationError{ code: msg }
    }
}

pub trait Validate {
    fn validate(&self) -> Result<(), ValidationError>;
}

static ALL: bool = false;

fn is_default<T: Default + PartialEq>(t: &T) -> bool {
    !ALL && t == &T::default()
}

#[cxx::bridge]
mod ffi {
    #[derive(Deserialize, Serialize, Debug, PartialEq)]
    pub struct Incoming {
        #[serde(default = "crate::Bool::<false>::v")]
        pdns_distibutes_queries: bool,
        #[serde(default = "crate::default_listen")]
        listen: Vec<String>,
    }

    #[derive(Deserialize, Serialize, Debug, PartialEq)]
    pub struct RecordCache {
        #[serde(default = "crate::U64::<1000000>::v")]
        size: u64,
    }
    #[derive(Deserialize, Serialize, Debug, PartialEq)]
    pub struct DNSSEC {
        #[serde(default = "crate::U64::<99>::v")]
        aggressive_cache_size: u64,
        #[serde(default = "crate::default_validation_mode")]
        validation: String,
    }
    #[derive(Deserialize, Serialize, Debug)]
    pub struct RecursorConfig {
        #[serde(default, skip_serializing_if = "crate::is_default")]
        incoming: Incoming,
        #[serde(default, skip_serializing_if = "crate::is_default")]
        record_cache: RecordCache,
        #[serde(default, skip_serializing_if = "crate::is_default")]
        dnssec: DNSSEC,
        #[serde(default)]
        a_string: String,
    }
    extern "Rust" {
        fn get_config_from_rust() -> RecursorConfig;
        fn get_config_from_rust2() -> RecursorConfig;
        fn get_config_from_rust3() -> RecursorConfig;
    }
}


//impl Default for ffi::RecordCache {
//    fn default() -> Self {
//        ffi::RecordCache { size: 99 }
//    }
//}

pub fn default_listen() -> Vec<String> {
    [ String::from("127.0.0.1"), String::from("::1") ].to_vec()
}

pub fn default_validation_mode() -> String {
    String::from("process")
}

//pub fn default_aggressive_cache_size() -> u64 {
//    101
//}

impl Default for ffi::Incoming {
    fn default() -> Self {
        let deserialized: ffi::Incoming = serde_yaml::from_str("").unwrap();
        deserialized
    }
}

impl Validate for ffi::Incoming {
    fn validate(&self) -> Result<(), ValidationError> {
        for val in &self.listen {
            let sa = SocketAddr::from_str(&val);
            let ip = IpAddr::from_str(&val);
            if !sa.map_or(false, |i| i.is_ipv4() || i.is_ipv6()) && !ip.map_or(false, |i| i.is_ipv4() || i.is_ipv6()) {
                let msg = format!("value `{}' is not an IP or IP:port combination", val);
                return Err(ValidationError::new(msg));
            }
        }
        Ok(())
    }
}

impl Default for ffi::DNSSEC {
    fn default() -> Self {
        let deserialized: ffi::DNSSEC = serde_yaml::from_str("").unwrap();
        deserialized
    }
}

impl Default for ffi::RecordCache {
    fn default() -> Self {
        let deserialized: ffi::RecordCache = serde_yaml::from_str("").unwrap();
        deserialized
    }
}

impl Default for ffi::RecursorConfig {
    fn default() -> Self {
        let deserialized: ffi::RecursorConfig = serde_yaml::from_str("").unwrap();
        deserialized
    }
}

fn get_config_from_rust() -> ffi::RecursorConfig {
    let config = ffi::RecursorConfig {
        record_cache: ffi::RecordCache { size: 1000 },
        dnssec: ffi::DNSSEC {
            aggressive_cache_size: 99,
            validation: String::from("process"),
        },
        incoming: ffi::Incoming{
            pdns_distibutes_queries: false,
            listen: [String::from("127.0.0.1"), String::from("[::2]:53")].to_vec(),
        },
        a_string: String::from("Hello via literal struct!"),
    };
    let serialized = serde_json::to_string(&config).unwrap();
    println!("JSON {}\n===", serialized);
    let deserialized: ffi::RecursorConfig = serde_json::from_str(&serialized).unwrap();
    println!("JSON deserialzed {:?}\n===", deserialized);
    let serialized = serde_yaml::to_string(&config).unwrap();
    println!("YAML {}\n===", serialized);
    config.incoming.validate().unwrap();
    config
}

fn get_config_from_rust2() -> ffi::RecursorConfig {
    let serialized = r###"{
    "record_cache" : {"size" : 1000 },
    "dnssec": {"aggressive_cache_size": 10000},
    "pdns_distibutes_queries":false,
    "a_string" : "Hello via literal struct from json!"
  }"###;
    let deserialized: ffi::RecursorConfig = serde_json::from_str(&serialized).unwrap();
    deserialized
}

fn get_config_from_rust3() -> ffi::RecursorConfig {
    let serialized = r###"
  record_cache:
    size: 10000
  dnssec:
    validation: validate
  pdns_distibutes_queries: false
  a_string: Hello via literal struct from yaml!
  "###;
    let deserialized: ffi::RecursorConfig = serde_yaml::from_str(&serialized).unwrap();
    deserialized
}
