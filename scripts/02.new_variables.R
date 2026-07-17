# ============================================================================
# This dataset is used to create new variables


# ============================================================================
# Libraries and paths
# ============================================================================
library(dplyr)
raw <- "data/raw"
int <- "data/intermediate"


df <- read.csv(file.path(int, "data_homemade_station.csv"))
df$entry_id <- NULL
head(df)


# ============================================================================
df_t <- df %>%
  mutate(day = as.Date(ts)) %>%
  group_by(day) %>%
  summarise(
    Tmin = min(T, na.rm = TRUE),
    Tmax = max(T, na.rm = TRUE),
    Tmean = mean(T, na.rm = TRUE),
    Trange = Tmax - Tmin,
    Rtot = sum(prec, na.rm = TRUE),
    Rmax = max(intens, na.rm = TRUE),
    Pmean = mean(press, na.rm = TRUE),
    Prange = max(press, na.rm = TRUE) - min(press, na.rm = TRUE),
    RHmean = mean(RH, na.rm = TRUE),
    RHmin = min(RH, na.rm = TRUE),
    RHmax = max(RH, na.rm = TRUE),
    .groups = "drop"
)

df_t <-df_t %>%
  mutate(
    dP = Pmean - lag(Pmean),         ## Variazione rispetto al D precedente
    Pz = scale(Pmean)[,1],           ## z-score
    dT = Tmean - lag(Tmean)          ## T rispetto al D precedente
  )


# ============================================================================
write.csv(df_t, file.path(int, "2.new_vars.csv"))



