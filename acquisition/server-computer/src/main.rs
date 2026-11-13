use tokio::{
    fs,
    io::{AsyncReadExt, BufReader},
    net::{TcpListener, TcpStream},
};

const DOSSIER: &str = "uploads";
const TAILLE_MAX: usize = 10 * 1024 * 1024; // 10 Mo max par fichier

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    fs::create_dir_all(DOSSIER).await?;
    let listener = TcpListener::bind("0.0.0.0:9000").await?;
    println!("En écoute sur 0.0.0.0:9000");

    loop {
        let (stream, addr) = listener.accept().await?;
        println!("{addr} connecté");
        tokio::spawn(async move {
            if let Err(e) = handle(stream).await {
                eprintln!("{addr}: {e}");
            }
            println!("{addr} déconnecté");
        });
    }
}

async fn handle(stream: TcpStream) -> Result<(), Box<dyn std::error::Error>> {
    stream.set_nodelay(true)?;
    let mut r = BufReader::new(stream);

    loop {
        // 1 octet : longueur du nom (EOF ici = déconnexion propre)
        let n = match r.read_u8().await {
            Ok(n) => n as usize,
            Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => return Ok(()),
            Err(e) => return Err(e.into()),
        };

        // 4 octets : taille du contenu
        let taille = r.read_u32().await? as usize;
        if taille > TAILLE_MAX {
            return Err(format!("fichier trop gros : {taille} octets").into());
        }

        // nom
        let mut nom = vec![0u8; n];
        r.read_exact(&mut nom).await?;

        // contenu
        let mut contenu = vec![0u8; taille];
        r.read_exact(&mut contenu).await?;

        let nom = String::from_utf8_lossy(&nom).into_owned();
        if !nom_valide(&nom) {
            eprintln!("nom refusé : {nom:?}");
            continue; // on a déjà tout lu, le flux reste synchronisé
        }

        let chemin = format!("{DOSSIER}/{nom}");
        fs::write(&chemin, &contenu).await?;
        println!("{chemin} ({taille} octets)");
    }
}

// Empêche d'écrire en dehors du dossier
fn nom_valide(nom: &str) -> bool {
    !nom.is_empty()
        && nom != "."
        && !nom.contains("..")
        && !nom.contains(['/', '\\', ':', '\0'])
}