//! RetroAchievements (Settings, "RetroAchievements"): the player signs in
//! here, once, and every profile's game is signed in too.
//!
//! The launcher sends the password to retroachievements.org over HTTPS and
//! keeps only the token the server answers with (`ra_user` and `ra_token`
//! in `launcher/settings.json`).  Before a game starts, the token is
//! written to `retroachievements.ini` in the profile's folder, the file
//! the game reads beside its `settings.ini` (docs/retroachievements.md).
//! The password is never written anywhere.

use std::fs;
use std::io;
use std::path::Path;
use std::time::Duration;

use serde::Deserialize;

const LOGIN_URL: &str = "https://retroachievements.org/dorequest.php";
const FILE: &str = "retroachievements.ini";

/// What a good sign-in gives back.
#[derive(Debug, PartialEq, Eq)]
pub struct Account {
    /// The name as the site writes it.
    pub user: String,
    pub token: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct Reply {
    #[serde(default)]
    success: bool,
    #[serde(default)]
    user: String,
    #[serde(default)]
    token: String,
    #[serde(default)]
    error: String,
}

/// The server's answer to a sign-in, as an account or a sentence.
pub fn parse_login(body: &str) -> Result<Account, String> {
    let r: Reply = serde_json::from_str(body).map_err(|_| "RetroAchievements answered in a way eidos does not know.".to_string())?;
    if r.success && !r.token.is_empty() {
        Ok(Account { user: r.user, token: r.token })
    } else if !r.error.is_empty() {
        Err(r.error)
    } else {
        Err("RetroAchievements did not sign you in.".to_string())
    }
}

fn form_encode(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => out.push(b as char),
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

/// Signs in with a password.  The password is used for this one request.
pub fn login(user: &str, password: &str, launcher_version: &str) -> Result<Account, String> {
    let user = user.trim();
    if user.is_empty() || password.is_empty() {
        return Err("Type your RetroAchievements user name and password.".to_string());
    }
    let agent = ureq::AgentBuilder::new()
        .timeout(Duration::from_secs(20))
        .user_agent(&format!("eidos-launcher/{launcher_version}"))
        .build();
    let body = format!("r=login2&u={}&p={}", form_encode(user), form_encode(password));
    match agent
        .post(LOGIN_URL)
        .set("Content-Type", "application/x-www-form-urlencoded")
        .send_string(&body)
    {
        Ok(resp) => {
            let text = resp.into_string().map_err(|e| format!("Could not read the answer: {e}"))?;
            parse_login(&text)
        }
        // the site answers a refused sign-in with a status and a body
        Err(ureq::Error::Status(_, resp)) => {
            let text = resp.into_string().unwrap_or_default();
            parse_login(&text)
        }
        Err(_) => Err("Could not reach retroachievements.org.".to_string()),
    }
}

/// The text of the file the game reads.  Names and tokens hold no
/// line breaks (the site's own tokens are plain letters and digits), so a
/// value that does is refused rather than written.
pub fn ini_text(user: &str, token: &str) -> Option<String> {
    let bad = |s: &str| s.is_empty() || s.chars().any(|c| c.is_control());
    if bad(user) || bad(token) {
        return None;
    }
    Some(format!("[retroachievements]\nuser = {user}\ntoken = {token}\n"))
}

/// Puts the sign-in into a profile folder for the game, or takes it out
/// when the player is signed out.  The file is the owner's only.
pub fn write_profile(profile_dir: &Path, user: &str, token: &str) -> io::Result<()> {
    let path = profile_dir.join(FILE);
    match ini_text(user, token) {
        Some(text) => {
            fs::create_dir_all(profile_dir)?;
            let tmp = path.with_extension("tmp");
            fs::write(&tmp, text)?;
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                fs::set_permissions(&tmp, fs::Permissions::from_mode(0o600))?;
            }
            fs::rename(tmp, path)
        }
        None => match fs::remove_file(&path) {
            Err(e) if e.kind() != io::ErrorKind::NotFound => Err(e),
            _ => Ok(()),
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_good_answer_is_an_account() {
        let a = parse_login(r#"{"Success":true,"User":"Mythic","Token":"abc123","Score":5}"#).unwrap();
        assert_eq!(a, Account { user: "Mythic".into(), token: "abc123".into() });
    }

    #[test]
    fn a_refusal_is_the_servers_sentence() {
        let e = parse_login(r#"{"Success":false,"Error":"Invalid user/password combination."}"#).unwrap_err();
        assert_eq!(e, "Invalid user/password combination.");
        assert!(parse_login("not json").is_err());
        assert!(parse_login(r#"{"Success":true}"#).is_err());
    }

    #[test]
    fn the_form_keeps_special_characters_whole() {
        assert_eq!(form_encode("a b&c=d%é"), "a%20b%26c%3Dd%25%C3%A9");
        assert_eq!(form_encode("Az09-_.~"), "Az09-_.~");
    }

    #[test]
    fn the_file_is_written_and_removed() {
        let t = tempfile::tempdir().unwrap();
        write_profile(t.path(), "Mythic", "abc").unwrap();
        let text = fs::read_to_string(t.path().join(FILE)).unwrap();
        assert_eq!(text, "[retroachievements]\nuser = Mythic\ntoken = abc\n");
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mode = fs::metadata(t.path().join(FILE)).unwrap().permissions().mode();
            assert_eq!(mode & 0o777, 0o600);
        }
        write_profile(t.path(), "", "").unwrap();
        assert!(!t.path().join(FILE).exists());
        write_profile(t.path(), "", "").unwrap(); // nothing to remove is fine
    }

    #[test]
    fn a_value_with_a_line_break_is_not_written() {
        assert_eq!(ini_text("a\nb", "t"), None);
        assert_eq!(ini_text("a", "t\r\n[x]"), None);
    }
}
