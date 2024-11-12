use std::net::SocketAddr;

use bytes::Bytes;
use http_body_util::{BodyExt, Full};
use hyper::{body::Incoming as IncomingBody, header, Method, Request, Response, StatusCode};
use hyper::server::conn::http1;
use hyper::service::service_fn;
use hyper_util::rt::TokioIo;
use tokio::net::TcpListener;
use tokio::runtime::Runtime;

use crate::recsettings::{*};

type GenericError = Box<dyn std::error::Error + Send + Sync>;
type Result<T> = std::result::Result<T, GenericError>;
type BoxBody = http_body_util::combinators::BoxBody<Bytes, hyper::Error>;

static NOTFOUND: &[u8] = b"Not Found";

fn full<T: Into<Bytes>>(chunk: T) -> BoxBody {
    Full::new(chunk.into())
        .map_err(|never| match never {})
        .boxed()
}

async fn hello(req: Request<IncomingBody>) -> Result<Response<BoxBody>> {
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
            let mut domain = String::from("");
            let mut typ = String::from("");
            let mut subtree = String::from("");
            if let Some(query) = req.uri().query() {
                for (k, v) in form_urlencoded::parse(query.as_bytes()) {
                    match &*k {
                        "domain" => domain = v.into_owned(),
                        "type" => typ = v.into_owned(),
                        "subtree" => subtree = v.into_owned(),
                        _ => { }
                    }
                }
            }
            let body = apiServerCacheFlush(domain.as_str(), typ.as_str(), subtree.as_str());
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

async fn serveweb() -> Result<()> {
    let addr = SocketAddr::from(([127, 0, 0, 1], 3000));

    // We create a TcpListener and bind it to 127.0.0.1:3000
    let listener = TcpListener::bind(addr).await?;

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

pub fn serveweb1() {
    println!("AXXXXXXXXXXXXXXXXXXXXXXX");
    println!("BXXXXXXXXXXXXXXXXXXXXXXX");
    std::thread::spawn(move || {
        let rt  = Runtime::new().unwrap();
        println!("CXXXXXXXXXXXXXXXXXXXXXXX");
        let handle = rt.handle();
        handle.block_on(async {
            let _ = serveweb().await;
            println!("DXXXXXXXXXXXXXXXXXXXXXXX");
        });
    });
}

