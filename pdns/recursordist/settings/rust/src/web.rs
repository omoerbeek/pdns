use std::net::SocketAddr;

use bytes::Bytes;
use http_body_util::{BodyExt, Full};
use hyper::{body::Incoming as IncomingBody, header, Method, Request, Response, StatusCode};
use hyper::server::conn::http1;
use hyper::service::service_fn;
use hyper_util::rt::TokioIo;
use tokio::net::TcpListener;
use tokio::runtime::Builder;
use tokio::task::JoinSet;

use std::io::ErrorKind;
use std::str::FromStr;

type GenericError = Box<dyn std::error::Error + Send + Sync>;
type MyResult<T> = std::result::Result<T, GenericError>;
type BoxBody = http_body_util::combinators::BoxBody<Bytes, hyper::Error>;

static NOTFOUND: &[u8] = b"Not Found";

fn full<T: Into<Bytes>>(chunk: T) -> BoxBody {
    Full::new(chunk.into())
        .map_err(|never| match never {})
        .boxed()
}

fn api_wrapper(handler: fn(&rustweb::Request, &mut rustweb::Response), request: &rustweb::Request, response: &mut rustweb::Response, headers: &mut header::HeaderMap)
{
    response.status = 200;
    // security headers
    headers.insert(header::ACCESS_CONTROL_ALLOW_ORIGIN, header::HeaderValue::from_static("*"));
    headers.insert(header::X_CONTENT_TYPE_OPTIONS, header::HeaderValue::from_static("nosniff"));
    headers.insert(header::X_FRAME_OPTIONS, header::HeaderValue::from_static("deny"));
    headers.insert(header::HeaderName::from_static("x-permitted-cross-domain-policies"), header::HeaderValue::from_static("none"));
    headers.insert(header::X_XSS_PROTECTION, header::HeaderValue::from_static("1; mode=block"));
    headers.insert(header::CONTENT_SECURITY_POLICY, header::HeaderValue::from_static("default-src 'self'; style-src 'self' 'unsafe-inline'"));

    println!("api_wrapper A0 Status {}", response.status);
    handler(request, response);
    println!("api_wrapper A Status {}", response.status);
}

async fn hello(rust_request: Request<IncomingBody>) -> MyResult<Response<BoxBody>> {
    let mut rust_response = Response::builder();
    let mut vars: Vec<rustweb::KeyValue> = vec![];
    if let Some(query) = rust_request.uri().query() {
        for (k, v) in form_urlencoded::parse(query.as_bytes()) {
            if k == "_" {  // jQuery cache buster
                continue;
            }
            let kv = rustweb::KeyValue{key: k.to_string(), value: v.to_string()};
            vars.push(kv);
        }
    }
    let mut request = rustweb::Request{body: vec!(), vars: vars};
    let mut response = rustweb::Response{status: 0, body: vec![], headers: vec![]};
    let headers = rust_response.headers_mut().expect("no headers?");
    match (rust_request.method(), rust_request.uri().path()) {
        (&Method::GET, "/metrics") => {
            rustweb::prometheusMetrics(&request, &mut response);
        }
        (&Method::PUT, "/api/v1/servers/localhost/cache/flush") => {
            api_wrapper(rustweb::apiServerCacheFlush, &request, &mut response, headers);
        }
        (&Method::GET, "/api/v1/servers/localhost/zones") => {
            println!("hello Status {}", response.status);
            api_wrapper(rustweb::apiServerZonesGET, &request, &mut response, headers);
        }
        (&Method::POST, "/api/v1/servers/localhost/zones") => {
            request.body = rust_request.collect().await?.to_bytes().to_vec();
            rustweb::apiServerZonesPOST(&request, &mut response);
        }
        _ => {
            // Return 404 not found response.
            response.status = StatusCode::NOT_FOUND.as_u16();
            response.body = NOTFOUND.to_vec();
        }
    }
    println!("B Status {}", response.status);
    let mut rust_response = rust_response
        .status(StatusCode::from_u16(response.status).unwrap())
        .body(full(response.body))?;
    for kv in response.headers {
        rust_response.headers_mut().insert(header::HeaderName::from_bytes(kv.key.as_bytes()).unwrap(), header::HeaderValue::from_str(kv.value.as_str()).unwrap());
    }
    Ok(rust_response)
}

async fn serveweb_async(listener: TcpListener) -> MyResult<()> {

    // We start a loop to continuously accept incoming connections
    loop {
        let (stream, _) = listener.accept().await?;

        // Use an adapter to access something implementing `tokio::io` traits as if they implement
        // `hyper::rt` IO traits.
        let io = TokioIo::new(stream);

        // Spawn a tokio task to serve multiple connections concurrently
        tokio::task::spawn(async move {
            // Finally, we bind the incoming connection to our `hello` service
            if let Err(err) = http1::Builder::new()
                // `service_fn` converts our function in a `Service`
                .serve_connection(io, service_fn(hello))
                .await
            {
                eprintln!("Error serving connection: {:?}", err);
            }
        });
    }
}

pub fn serveweb(addresses: &Vec<String>) -> Result<(), std::io::Error> {

    let runtime = Builder::new_current_thread()
        .worker_threads(1)
        .thread_name("rec/web")
        .enable_io()
        .build()?;

    let mut set = JoinSet::new();
    
    for addr_str in addresses {

        // Socket create and bind should happen here
        //let addr = SocketAddr::from_str(addr_str);
        let addr = match SocketAddr::from_str(addr_str) {
            Ok(val) => val,
            Err(err) => {
                let msg = format!("`{}' is not a IP:port combination: {}", addr_str, err);
                return Err(std::io::Error::new(ErrorKind::Other, msg));
            }
        };

        let listener = runtime.block_on(async {
            TcpListener::bind(addr).await
        });

        match listener {
            Ok(val) => {
                println!("Listening on {}", addr);
                set.spawn_on(serveweb_async(val), runtime.handle());
            },
            Err(err) => {
                let msg = format!("Unable to bind web socket: {}", err);
                return Err(std::io::Error::new(ErrorKind::Other, msg));
            }
        }
    }
    std::thread::Builder::new()
        .name(String::from("rec/rustweb"))
        .spawn(move || {
            runtime.block_on(async {
                while let Some(res) = set.join_next().await {
                    println!("{:?}", res);
                }
        });
    })?;
    Ok(())
}

#[cxx::bridge(namespace = "pdns::rust::web::rec")]
/*
 * Functions callable from C++
 */
mod rustweb {

    extern "Rust" {
        fn serveweb(addreses: &Vec<String>) -> Result<()>;
    }

    struct KeyValue
    {
        key: String,
        value: String,
    }

    struct Request
    {
        body: Vec<u8>,
        vars: Vec<KeyValue>,
    }

    struct Response
    {
        status: u16,
        body: Vec<u8>,
        headers: Vec<KeyValue>,
    }

    unsafe extern "C++" {
        include!("bridge.hh");
        fn prometheusMetrics(request: &Request, response: &mut Response);
        fn apiServerCacheFlush(request: &Request, response: &mut Response);
        fn apiServerZonesGET(request: &Request, response: &mut Response);
        fn apiServerZonesPOST(requst: &Request, response: &mut Response);
    }

}
