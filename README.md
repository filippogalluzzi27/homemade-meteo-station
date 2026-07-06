\# Stazione Meteo ESP32 + Analisi Climatica in R

Stazione meteorologica personale basata su \*\*ESP32\*\* con logging su SD e invio a \*\*ThingSpeak\*\*



Script \*\*R\*\* che scarica il feed, lo pulisce e genera un set completo di grafici climatici.



Schermatura Stevenson (legno); altezza 2m dal suolo.





Progetto pensato per il monitoraggio locale (contesto: Serre di Rapolano, \~330 m s.l.m., Toscana), con confronto verso la climatologia LaMMA/SIR Siena 1991–2020.





Codici sviluppati ed ottimizzati con l'assistenza di Claude



\---

\- \*\*Campionamento\*\*: ogni 10 minuti (blocco ancorato al minuto reale via RTC).

\- \*\*Ridondanza\*\*: ogni riga salvata anche su SD (`/meteo.csv`); se manca rete, buffer su SD (`/buffer.csv`) e reinvio automatico.



\---



\## 🔧 Hardware



| Componente | Funzione | Bus / Pin |

|---|---|---|

| ESP32 | MCU + WiFi | — |

| \*\*SHT31\*\* (Adafruit) | Temperatura + Umidità | I²C |

| \*\*BMP388\*\* (Adafruit) | Pressione | I²C |

| \*\*DS3231\*\* | RTC (orario preciso, backup batteria) | I²C |

| \*\*Modulo SD\*\* | Logging locale + buffer offline | SPI, `CS = GPIO10` |

| \*\*Pluviometro a bascula\*\* | Pioggia (interrupt su impulso) | `GPIO2`, `INPUT\_PULLUP` |



> \*\*Costante calibrazione pluviometro\*\*: `RAIN\_PER\_TIP = 0.3 mm/tip`.



\---



\## 💾 Firmware (Arduino / ESP32)



\### Librerie richieste

`WiFi`, `HTTPClient`, `Wire`, `SPI`, `SD`, `RTClib`, `Adafruit\_SHT31`, `esp\_task\_wdt`



\### Configurazione (in testa allo sketch)

```cpp

const char\* ssid     = "...";

const char\* password = "...";

String apiKey        = "...";   // Write API Key del canale ThingSpeak

\#define SD\_CS    10

\#define RAIN\_PIN  2

```



\### Funzionalità

\- \*\*Blocco 10 min\*\*: trigger sul minuto reale (`now.minute() % 10 == 0`), immune a drift del loop.

\- \*\*Pioggia\*\*: conteggio impulsi via ISR con \*\*debounce 400 ms\*\*; calcolo mm/blocco e \*\*intensità max (mm/h)\*\* tra impulsi consecutivi (entro 1h), con filtro anti-spike (`MAX\_VALID\_INTENSITY = 500`).

\- \*\*Contatori separati\*\* per blocco e intensità (evita il bug del `tipsBlock = 0`).

\- \*\*Invio ThingSpeak\*\* con rate-limit e \*\*timeout HTTP 5 s\*\* (non blocca il loop); `lastSend` aggiornato sempre.

\- \*\*Buffer offline\*\*: se l'invio fallisce, la riga va in `/buffer.csv` e viene reinviata quando torna la rete.

\- \*\*Sync NTP giornaliera\*\* dell'RTC (usa `checkWiFi()`, non riconnette inutilmente).

\- \*\*Watchdog 30 s\*\*: reset automatico in caso di blocco.



\### Campi inviati a ThingSpeak

| Campo | Grandezza | Unità |

|---|---|---|

| `field1` | Temperatura | °C |

| `field2` | Umidità relativa | % |

| `field3` | Pioggia (per blocco 10') | mm |

| `field4` | Intensità max | mm/h |

| `field5` | Pressione assoluta | hPa |



\---



\## 📊 Analisi in R



Script \*\*`analisi\_meteo.R`\*\* — solo pacchetti \*\*base R\*\*, nessuna dipendenza esterna.



\### Requisiti dati

Esporta il feed da ThingSpeak in `data/raw/feeds.csv` con colonne `created\_at`, `entry\_id`, `field1…field5`.



\### Esecuzione

```bash

Rscript analisi\_meteo.R

```

Tutti i grafici `.png` (etichette in italiano) vengono salvati in `output/`.



\### Preprocessing

\- \*\*Correzione black-out\*\* basata su pressione sentinella e cadute anomale (`field5`).

\- \*\*Rimozione outlier\*\* su range fisici + \*\*interpolazione temporale\*\* dei `NA`.

\- Variabili derivate: \*\*VPD\*\* (Buck), dewpoint, umidità specifica, bulbo umido, Heat Index, Humidex, indice di Thom, pressione ridotta al livello del mare.



\### Grafici generati



| Gruppo | Contenuto |

|---|---|

| \*\*A\*\* – Temperatura | giornaliera/mensile, escursione termica (DTR), soglie termiche, \*\*onde di calore\*\* (soglia climatologica + percentile), gradi-giorno (GDD/HDD/CDD), anomalie vs clima 91–20 |

| \*\*B\*\* – Umidità \& comfort | VPD, punto di rugiada, umidità specifica, caldo percepito, bulbo umido, indice di Thom |

| \*\*C\*\* – Precipitazioni | totali + cumulata, giorni piovosi \& SDII, intensità (max 1h/24h), periodi secchi/piovosi consecutivi, distribuzione oraria |

| \*\*D\*\* – Pressione | serie + tendenza barica Δ3h, pressione slm, rilevamento fronti |

| \*\*E\*\* – Climatologia | climogramma Walter-Lieth (vs clima locale), ETP Hargreaves, bilancio idrico P−ETP |

| \*\*F\*\* – Cicli \& trend | ciclo diurno medio, heatmap calendario (T, VPD), trend \*\*Mann-Kendall + Sen\*\*, boxplot mensile |

| \*\*G\*\* – Qualità dato | correzione black-out, completezza serie |



\---



\## 📁 Struttura repository



```

.

├── scripts/

│   └── stazione\_meteo.ino          # sketch ESP32 per Arduino

│   └── analisi\_meteo.R             # analisi + grafici

├── data/

│   └── raw/

│       └── feeds.csv           # export ThingSpeak

├── output/                     # PNG generati (auto)

└── README.md

```



