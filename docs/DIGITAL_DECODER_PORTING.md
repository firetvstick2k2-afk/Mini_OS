# Digitale Decoder – Portierungsplan

## Ziel und feste Hardwaregrenze

Die digitalen Decoder müssen auf dem ATS Mini V4 ohne Hardwareänderung laufen. Es werden keine zusätzlichen Kabel, Mikrofone, Soundkarten, Audio-Codecs oder Platinenmodifikationen vorausgesetzt.

Der vorhandene Audioeingang ist der in `src/cw/CwDecoderService.cpp` verwendete GPIO11-Pfad mit 4.000 Samples/s. Dieser wird als gemeinsamer Sample-Producer weiterverwendet. Es darf keinen separaten ADC-Sampler pro Decoder geben. WLAN ist im aktuellen CW-Aufnahmepfad während des Decodierens ausgeschaltet; diese Einschränkung muss bei der gemeinsamen Architektur berücksichtigt werden.

## Quellenauswahl

Primäre offene Referenz ist [RX Analyzer / ham_decoder](https://github.com/joaquimorg/ham_decoder), Upstream-Revision:

`13c8934b52e93728ee01f5efc85ce015f6fbdd2f`

Das Projekt ist MIT-lizenziert. Die Decoderalgorithmen werden selektiv übernommen; seine Freenove-/DevKit-Audio-Hardware, Display- und Weboberfläche werden nicht portiert.

### Priorität 1 – RTTY

Der erste DSP-Port liegt unter `src/digital/rtty/`.

- ITA2/Baudot mit 5 Datenbits
- 45,45 / 50 / 75 Baud
- Automatische, normale und invertierte Polarität
- AFC-/Tonerkennung wird über die aufrufende Ebene angebunden
- Der Decoder nimmt einen Float-Sample-Stream entgegen; er greift selbst nicht auf GPIO oder ADC zu.

Die aktuelle Portierung ist nur der DSP-Kern. Tonpaarerkennung, Audio-Fan-out, Bedienung, Display und Gerätetest sind noch offen.

### Priorität 2 – FT8 und FT4

Referenz ist `kgoba/ft8_lib`, eingebettet im RX-Analyzer-Projekt unter MIT-Lizenz. Der dortige Monitor konfiguriert seine Sample-Rate über `monitor_config_t.sample_rate`; daher wird zuerst eine 4-kHz-Konfiguration gegen die vorhandenen Testvektoren geprüft. Das vermeidet einen zweiten Sampler und erhält die gemeinsame GPIO11-Aufnahme.

FT8/FT4 benötigen eine genaue UTC-Slot-Zeitbasis (15 s / 7,5 s). Die bestehende Systemzeit muss geprüft und ggf. vor dem Decoderbetrieb synchronisiert werden. Ohne verlässliche Zeit darf kein erfolgreicher FT8/FT4-Betrieb vorgetäuscht werden.

### Weitere Decoder (Bonus)

PSK31, SSTV und WEFAX bleiben nach RTTY und FT8/FT4. Vor jeder Übernahme sind benötigte Audio-Bandbreite, Sample-Rate, RAM-Bedarf und die Möglichkeit der Verarbeitung über denselben 4-kHz-Pfad zu prüfen. SSTV/WEFAX werden nicht mit einer zusätzlichen Hardware-Audioquelle erkauft.

## Architekturregeln

1. Eine gemeinsame Audioaufnahme auf GPIO11; mehrere Decoder erhalten Samples über eine klar definierte Verteilung.
2. Decoder-DSP bleibt hardware- und UI-unabhängig.
3. CW muss mit identischem Bedienverhalten und ohne Qualitätsverlust erhalten bleiben.
4. Decoder bekommen getrennte Module unter `src/digital/<mode>/`.
5. Keine Änderungen an RDS, RadioDNS, Senderlogos oder bestehenden Layouts im Rahmen der Decoder-DSP-Portierung.
6. Jeder Port wird mit Host-Testvektoren, statischen Prüfungen und anschließend Compile-only validiert. Der Nutzer startet Builds selbst; kein Flash ohne ausdrückliche Freigabe.

## Status

- [x] Quellenauswahl und Lizenzprüfung
- [x] RTTY-DSP-Kern auf 4 kHz portiert
- [ ] Gemeinsamer Audio-Fan-out mit CW
- [ ] RTTY-Tonerkennung und Bedienung
- [ ] FT8/FT4-Bibliothek und Slot-Decoder portiert
- [ ] Host-Testvektoren für RTTY und FT8/FT4
- [ ] Menü- und Displayintegration
- [ ] Compile-only-Build durch den Nutzer
- [ ] Gerätetest durch den Nutzer

Es wurde noch kein Build oder Flash gestartet. Der RTTY-DSP-Kern ist noch nicht mit der Gerätebedienung verbunden und somit noch keine verfügbare Funktion.

## FT8/FT4 Bibliotheksimport (2026-11-17)

- Die MIT-lizenzierte Originalbibliothek `kgoba/ft8_lib` wurde unter `src/digital/ftx/ft8_lib/` eingecheckt (Unterordner `ft8`, `common`, `fft` und Original-`LICENSE`).
- `common/monitor.c` und `.h` verwenden lokal auflösbare relative Include-Pfade; alle weiteren Dateien bleiben zunächst möglichst unverändert.
- **Stand:** Bibliotheksquellen importiert; noch kein ATS-Mini-Wrapper, keine gemeinsame Audio-Fan-out-Anbindung, keine Slot-Synchronisation, keine Decoder-UI und kein Build-/Hardwaretest. Nicht als funktionsfähig ausgeben.
- **Nächster Schritt:** Include-/Link-Kompatibilität und vollständige Abhängigkeiten prüfen; anschließend FT8/FT4-RX-Service mit begrenztem Speicherbudget und Zeitfenstern auf Basis des vorhandenen GPIO11-Samplers integrieren. Den CW-Sampler nicht duplizieren.
- **Lizenzprüfung:** Bei Distribution zusätzlich die Herkunft und Lizenzbedingungen des eingebetteten KissFFT-Codes nachvollziehbar dokumentieren.
