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

use crate::recsettings::{*};
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

async fn hello(req: Request<IncomingBody>) -> MyResult<Response<BoxBody>> {
    match (req.method(), req.uri().path()) {
        (&Method::GET, "/metrics") => {
            let body = prometheusMetrics();
            let resp = Response::builder()
                .status(StatusCode::OK)
                .header(header::CONTENT_TYPE, "text/plain")
                .body(full(body))?;
            Ok(resp)
        }
        (&Method::PUT, "/api/v1/servers/localhost/cache/flush") => {
            let mut vec: Vec<KeyValue> = vec![];
            if let Some(query) = req.uri().query() {
                for (k, v) in form_urlencoded::parse(query.as_bytes()) {
                    let kv = KeyValue{key: k.to_string(), value: v.to_string()};
                    vec.push(kv);
                }
            }
            let body = apiServerCacheFlush(&vec);
            let resp = Response::builder()
                .status(StatusCode::OK)
                .header(header::CONTENT_TYPE, "application/json")
                .body(full(body))?;
            Ok(resp)
        }
        (&Method::GET, "/api/v1/servers/localhost/zones") => {
            let body = apiServerZonesGET();
            let resp = Response::builder()
                .status(StatusCode::OK)
                .header(header::CONTENT_TYPE, "application/json")
                .body(full(body))?;
            Ok(resp)
        }
        _ => {
            // Return 404 not found response.
            Ok(Response::builder()
                .status(StatusCode::NOT_FOUND)
                .body(full(NOTFOUND))
                .unwrap())
        }
    }
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

