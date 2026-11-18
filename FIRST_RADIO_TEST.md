# Primo collaudo radio M1c

Il primo collaudo bidirezionale **DRA1 è NOT RUN**. La procedura corrente, con le autorizzazioni da ottenere, è in [M1C_BENCH_TEST.md](M1C_BENCH_TEST.md). Lo stato dei binari e delle prove è in [STATUS.md](STATUS.md).

- **Evidenza storica separata.** Un ricevitore S3 minimale ha ricevuto sul canale 6 dieci beacon DRB2 validi confermati, senza errori CRC nei report completi. La NVS del radar era identica prima e dopo. Il banco non ha verificato ACK, unicast cifrato o pairing DRA1.
- **Prerequisiti M1c.** Entrambe le schede via USB, antenna esterna C6 collegata prima di alimentare, attuatori isolati e backup in ROM verificati. Il C6 precedente usava l'antenna interna; il candidato standard del banco usa quella esterna. Sul radar associato il canale segue l'AP.
- **Interpretazione.** Beacon DRB2, accodamento locale e callback TX del driver non provano una risposta applicativa. Per DRA1 verificare la crescita di `confirmed` su entrambi i dispositivi dopo l'handshake. I test negativi fisici richiedono una fixture separata.
