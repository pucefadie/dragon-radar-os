# Primo collaudo radio M1c

Il primo collaudo bidirezionale DRA1 ha superato le prove di banco con la PMK comune derivata e il profilo S3/C6 dedicato. Il tentativo con le PMK locali distinte era fallito. [STATUS.md](STATUS.md) contiene i risultati, le versioni e le verifiche ancora aperte. La procedura è in [M1C_BENCH_TEST.md](M1C_BENCH_TEST.md).

- **Evidenza storica separata.** Un ricevitore S3 minimale ha ricevuto sul canale 6 dieci beacon DRB2 validi confermati, senza errori CRC nei report completi. La NVS del radar era identica prima e dopo. Il banco non ha verificato ACK, unicast cifrato o pairing DRA1.
- **Prerequisiti M1c.** Usare sola USB e attuatori isolati. Collegare l'antenna esterna del C6 prima di alimentarlo. Il radar senza batteria è entrato manualmente in ROM. Usare la build S3 di banco separata dopo approvazione. Prima di ogni scrittura servono backup in ROM verificati. Il C6 precedente usava l'antenna interna; il candidato standard del banco usa quella esterna. Sul radar associato il canale segue l'AP.
- **Interpretazione.** Per DRA1 verificare la crescita di `confirmed` su entrambi i dispositivi dopo l'handshake. Beacon DRB2 e callback TX del driver restano evidenze distinte. La fixture opzionale S3 per i test negativi richiede approvazione prima dell'installazione. Seguire la procedura di banco per configurazione elettrica, USB e ROM.
