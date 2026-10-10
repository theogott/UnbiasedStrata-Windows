# Strata: Sicherheitsvergleich vom 10.10.2026

Verglichen wird die offizielle Version **0.1.41**, Commit `fb58e0dbc8399662c0e47c76578c6e878b14f6cf`. Am 10.10.2026 erneut als neueste GitHub-Version bestätigt. Dieser Bericht ist eine datierte Momentaufnahme, keine fortlaufende Sicherheitszertifizierung. Die untersuchten Punkte sind **teilweise behoben**. UnbiasedStrata ist nicht nachweislich insgesamt sicherer. Die Prüfung erfolgte durch einen separaten Subagenten anhand aktueller Quellen und lokaler Dateien. Keine Inferenz oder GPU-Tests wurden gestartet.

| Punkt | Offizielles Strata 0.1.41 |
| --- | --- |
| Engine-ZIP ohne Hashprüfung | Teilweise behoben: GitHub-Asset-Digest wird geprüft. Bei fehlendem Digest, Spiegelquellen oder `STRATA_SKIP_SHA256=1` wird mit Warnung fortgefahren. Unser bereits ausgeführtes Update wurde tatsächlich gegen den Digest geprüft. |
| Fehlende Modellrevision fällt auf `main` zurück | Weiter vorhanden; dann können festgelegte Tensor-Hashes übersprungen werden. Der für diese Prüfung verwendete Modelldownloader verwendet dagegen eine feste Revision und verlangt vollständige Hashübereinstimmung. |
| `setup --check` kann Daten verändern | Weiter vorhanden: Datenordner-Ermittlung läuft vor dem Check-Abbruch und kann Dateien zusammenführen oder Einstellungen speichern. Keine nachgewiesene ausnutzbare Schwachstelle. |
| Überlange HTTP-Anfragen | Begrenzung vorhanden, Standard 256 MiB. |
| Browser-/DNS-Rebinding-Schutz | Host-/Origin-Prüfung, API-Key-Unterstützung und eingeschränktes CORS sind vorhanden. |
| Weitere SSRF-, Bilddatei-, MCP-, Archiv- und Parser-Härtungen | Nicht vollständig übernommen; PR #1126 ist offen und nicht zusammengeführt. |

Die ursprüngliche begrenzte Prüfung in [Issue #544](https://github.com/Niko1221/Strata/issues/544#issuecomment-6009032061) meldet keine bekannte ausnutzbare Schwachstelle. [PR #1126](https://github.com/Niko1221/Strata/pull/1126) enthält weitergehende vorgeschlagene Änderungen; ein geschlossener Vorgänger-PR ist kein Beleg einer Übernahme. [Offizielle Version 0.1.41](https://github.com/Niko1221/Strata/releases/tag/v0.1.41).

UnbiasedStrata entfernt Installer und Python-Server, wodurch diese automatischen Downloadpfade entfallen. Laut [Provenienz](https://github.com/robertkarl/UnbiasedStrata/blob/main/UPSTREAM.md) bleibt seine Engine jedoch auf dem unveränderten Stand 0.1.39. Das [C++-Frontend am geprüften Commit](https://github.com/robertkarl/UnbiasedStrata/blob/d0a44ea7e757c59fa35a78b4b98d0c4bb44ce2cf/frontend/main.cpp) hat keine entsprechenden API-Key-, Host- oder Origin-Prüfungen. Eine Windows-Portierung allein löst das nicht.

Die aktuell installierte Konfiguration bindet an `127.0.0.1:8080`; Vision, MCP, before_load und CORS sind nicht gesetzt. Empfehlung: 0.1.41 vorerst behalten oder den Windows-Fork zusätzlich härten, bevor er die alte Installation ersetzt. Kein Nachweis für Malware oder Telemetrie gefunden; keine vollständige Sicherheitsprüfung oder Exploit-Reproduktion. Das konkrete YouTube-Video von Unbiased Bob wurde nicht identifiziert, daher lassen sich ihm keine bestimmten Aussagen sicher zuordnen.

## Einordnung der Schwere

Die folgenden Einschätzungen sind qualitative Risikoeinordnungen, keine bestätigten CVE- oder CVSS-Bewertungen. Für die jetzige lokale Konfiguration ist die Exposition begrenzt; Loopback allein ist jedoch keine vollständige Sicherheitsgrenze gegen andere lokale Programme oder Browserangriffe.

| Bereich | Mögliche Auswirkung / aktuelle Einordnung |
| --- | --- |
| Ungeprüftes Engine-Archiv | Manipulierter ausführbarer Code hätte hohe Auswirkungen. Unser 0.1.41-Archiv wurde gegen den GitHub-Digest geprüft; die verbleibenden Installer-Ausnahmen sind weiterhin ein Integritätsrisiko. |
| Fallback auf ungepinnte Modelle | Mittleres Integritätsrisiko bei verschwundener Revision. Unsere Downloads verwendeten eine feste Revision und vollständige Hashprüfungen, also keinen solchen Fallback. |
| Vision / MCP / Netzwerkfreigabe / Parser | Potenziell erhebliche Auswirkungen je nach aktivierter Funktion und nicht vertrauenswürdiger Eingabe. Offene Hardening-Vorschläge sind kein Beleg einer praktisch ausnutzbaren Lücke. Vision/MCP/Hooks sind hier nicht konfiguriert; der Server ist nicht für das LAN freigegeben. |
| HTTP-Ressourcenverbrauch | Verfügbarkeitsrisiko; die 256-MiB-Standardgrenze ist großzügig. Extern erreichbar wäre das relevanter. |
| `setup --check` verändert Daten | Niedrige Schwere als überraschender Seiteneffekt; keine nachgewiesene ausnutzbare Sicherheitslücke. |

## Separater Windows-Fork

Das offizielle Strata 0.1.41 bleibt installiert. Der Windows-Fork ist separat gebaut und verlangt Bearer-Authentifizierung auf allen Endpunkten, Loopback-Bindung, Host-/Origin-Prüfung, höchstens 1 MiB Anfragegröße sowie begrenzte Worker/Queue und Lesezeiten. API-Key-Dateien werden vor dem Öffnen auf lokale Laufwerke und nicht umgeleitete Verzeichnisse geprüft; Lesen und Schlüsselgröße sind begrenzt. Zusätzliche Engine-Argumente sind durch eine Allowlist eingeschränkt.

Windows-Prozess- und HTTP-CTest bestehen. Der Subagent hat die drei gezielt behobenen Befunde erneut geprüft; der letzte Key-Datei-Befund ist innerhalb der ausdrücklich vorausgesetzten vertrauenswürdigen lokalen Dateisystemgrenze geschlossen. Bösartige gleichzeitige lokale Dateisystemänderungen bleiben außerhalb dieser Grenze. Keine echte Modell-Inferenz oder GPU-Ausführung getestet. Die unveränderte Qwen-Engine 0.1.39 übernimmt damit nicht automatisch sämtliche Verbesserungen des offiziellen 0.1.41.

## DeepSeek-Erweiterung und Stand der Validierung

Der Windows-Fork enthält inzwischen einen **separaten DeepSeek-V4.1-Backendprozess** auf Basis eines gepinnten JigSawPT-llama.cpp-Forks. Die Aussage zur unveränderten Strata-Engine 0.1.39 betrifft den **Qwen-Pfad**. Die DeepSeek-Erweiterung ist keine Übernahme sämtlicher Sicherheitsänderungen des offiziellen Strata und kein vollständiger Audit des zusätzlichen Backends.

Der DeepSeek-HTTP-Pfad verwendet ebenfalls die lokale Authentifizierung und Host-/Origin-Prüfung. Exponiert sind nur Health, Modellliste und Text-Chat; Bildquellen, Modellrouter und MCP-Proxy sind in diesem Pfad nicht freigegeben. Der Supervisor verwaltet seinen eigenen Windows-Jobprozess und filtert vererbte Backend-Steuervariablen.

Build und modellfreie Windows-Prozess-/HTTP-Tests sind geprüft. Zum Veröffentlichungszeitpunkt dieses Vergleichs läuft der Modelldownload; der angeforderte reale GPU-Benchmark wartet auf alle verifizierten Dateien. Eine große Context-Einstellung, erfolgreiche Kompilierung oder synthetische SSE-Antworten belegen weder echte Inferenzqualität noch ausreichende VRAM/RAM. Ergebnisse späterer Benchmarks müssen gesondert dokumentiert werden.

## Öffentliche Belegstellen

- [Offizielles setup.py am geprüften Commit](https://github.com/Niko1221/Strata/blob/fb58e0dbc8399662c0e47c76578c6e878b14f6cf/setup.py): Engine-Digestprüfung, Integritätsausnahmen, Modellrevision und Check-Ablauf.
- [Offizielles mtp_fetch.py am geprüften Commit](https://github.com/Niko1221/Strata/blob/fb58e0dbc8399662c0e47c76578c6e878b14f6cf/tools/mtp_fetch.py): Revision-Fallback und Hashausnahmen.
- [Offizieller Python-Server am geprüften Commit](https://github.com/Niko1221/Strata/blob/fb58e0dbc8399662c0e47c76578c6e878b14f6cf/serve/server.py): HTTP, Bildquellen und Hooks.
- [HTTP-Zugriffsschutz unseres Windows-Forks](https://github.com/theogott/UnbiasedStrata-Windows/blob/3c16f3f853e83da26863e08ca31f2a6943f19a0a/frontend/http_access.h).
- [Lokale Key-Dateiprüfung](https://github.com/theogott/UnbiasedStrata-Windows/blob/3c16f3f853e83da26863e08ca31f2a6943f19a0a/frontend/local_key.h).
- [DeepSeek-Routen und Text-Request-Prüfung](https://github.com/theogott/UnbiasedStrata-Windows/blob/3c16f3f853e83da26863e08ca31f2a6943f19a0a/frontend/deepseek_http.h).
- [Gepinnte DeepSeek-Quelle und Windows-Patch](https://github.com/theogott/UnbiasedStrata-Windows/tree/3c16f3f853e83da26863e08ca31f2a6943f19a0a/backends/deepseek41).

Die Links auf bestimmte Commits fixieren die geprüfte Quellenbasis. Die Einstufungen sind qualitative Bewertungen; kein Exploit wurde reproduziert und keine CVSS-Bewertung vergeben. Automatisierte Coding- und Review-Unterstützung wurde verwendet.
