# ============================================================================
# This dataset is used to clean the data.
# Input -> feed.csv -> File downloaded from ThingSpeak.
# field1 = T (°C)
# field2 = RH (%)
# field3 = rain in 10 minutes (mm)
# field4 = rain max intensity within 1h interval (mm/h)
# field5 = absoulte pressure (hPa)


# ============================================================================
# Libraries and paths
# ============================================================================
raw <- "data/raw"
int <- "data/intermediate"



# ============================================================================
# Configuration
# ============================================================================
P <- list(
  INPUT_PATH = file.path(raw, "feeds.csv"), 
  OUTPUT_PATH = file.path(int, "data_homemade_station.csv"),
  DPI        = 130,
  TZ         = "Europe/Rome",
  QUOTA_M = 330,    # elevation
  LAT     = 43.30,  # latitude (ETP Hargreaves)
  TIP_MM = 0.29,    # mm of tipping bucket for rain gauge
  
  COL_T="field1", COL_RH="field2", COL_PREC="field3", COL_INT="field4", COL_PRESS="field5",
  COL_TIME="created_at", COL_ID="entry_id",
  
  # In caso di black-out, il sensore pressione va ad un valore predefinito
  PRESS_SENTINELLA = 815.79, SENTINELLA_TOLL = 3.0, BLACKOUT_DROP_HPA = 6.0,

  # per rimozione outliners -> range fisici non plausibili
  LIM = list(T=c(-25,50), RH=c(0,100), press=c(900,1060), prec=c(0,50), intens=c(0,300))
)



# ============================================================================
# Gestione NA -> interpolazione
# ============================================================================
interp_time <- function(ts, v){
  xn <- as.numeric(ts); ok <- !is.na(v)
  if(sum(ok) < 2) return(v)
  approx(xn[ok], v[ok], xout=xn, rule=2)$y
}



# ============================================================================
# Funzione di upload e pulizia 
# ============================================================================
cleaning <- function(){
  df <- read.csv(P$INPUT_PATH, stringsAsFactors=FALSE)
  
  # rinomina le variabili e converte i valori
  ren <- c(T=P$COL_T, RH=P$COL_RH, prec=P$COL_PREC, intens=P$COL_INT, press=P$COL_PRESS)
  for(nm in names(ren)) df[[nm]] <- suppressWarnings(as.numeric(df[[ren[nm]]]))
  
  # timestamp: "2024-01-01T12:00:00+01:00" -> POSIXct, poi tz locale
  s  <- sub("T"," ", df[[P$COL_TIME]])
  s  <- sub("([+-][0-9]{2}):([0-9]{2})$","\\1\\2", s)
  ts <- as.POSIXct(s, format="%Y-%m-%d %H:%M:%S%z", tz="UTC"); attr(ts,"tzone") <- P$TZ
  df$ts <- ts
  
  # elimina righe con lo stesso entry_id. e riordina il dataframe per timestamp crescente
  if(P$COL_ID %in% names(df)) df <- df[!duplicated(df[[P$COL_ID]]),]
  df <- df[order(df$ts),]; ts <- df$ts
  
  # Correzione in caso di black-out 
  press <- df$press; dp <- c(NA, diff(press)); dp_next <- c(dp[-1], NA)
  sent <- abs(press - P$PRESS_SENTINELLA) < P$SENTINELLA_TOLL
  drop <- !is.na(dp) & !is.na(dp_next) & (dp < -P$BLACKOUT_DROP_HPA) & (dp_next > P$BLACKOUT_DROP_HPA)
  blackout <- (sent | drop); blackout[is.na(blackout)] <- FALSE
  for(c in c("T","RH","prec","intens","press")){
    v <- df[[c]]; v[blackout] <- NA; df[[c]] <- interp_time(ts, v)
  }

  # Correzione outliners
  for(c in names(P$LIM)){
    v <- df[[c]]; bad <- (v < P$LIM[[c]][1]) | (v > P$LIM[[c]][2]); bad[is.na(bad)] <- FALSE
    if(any(bad)){ v[bad] <- NA; df[[c]] <- interp_time(ts, v) }
  }
  df$RH <- pmin(pmax(df$RH, 0), 100)
  
  df
}


# ============================================================================
# Creazione cartella, run e salvataggio
# ============================================================================
dir.create(int, showWarnings=FALSE, recursive=TRUE)
df <- cleaning()

# output: solo colonne pulite (ts in ISO, TZ locale)
out <- data.frame(
  ts     = format(df$ts, "%Y-%m-%d %H:%M:%S", tz=P$TZ),
  T      = df$T,
  RH     = df$RH,
  prec   = df$prec,
  intens = df$intens,
  press  = df$press
)
if(P$COL_ID %in% names(df)) out$entry_id <- df[[P$COL_ID]]

write.csv(out, P$OUTPUT_PATH, row.names=FALSE)






