/**
 * @file voice.h
 * @brief Composition des annonces vocales à partir de fragments WAV.
 *
 * Fichiers sur la carte microSD (WAV PCM 16 bits mono 16 kHz) :
 *   /VOIX/Mnnn.WAV  messages système (table voice_texts[])
 *   /VOIX/Nnnn.WAV  nombres 1 à 32 (« un », « deux »…)
 *   /VOIX/Znnn.WAV  noms de zones enregistrés (optionnels, zone_cfg.voice_id)
 * Les mêmes fragments, convertis en AMR, sont copiés dans le module 4G pour
 * être joués à distance pendant un appel.
 */
#ifndef VOICE_H
#define VOICE_H
#include "alarm_config.h"
#include "alarm_events.h"

typedef enum {
    VM_NONE = 0,
    VM_ARMED_AWAY, VM_ARMED_STAY, VM_DISARMED, VM_EXIT_DELAY, VM_ENTRY_DELAY,
    VM_INTRUSION, VM_FIRE, VM_TAMPER, VM_TECHNICAL, VM_ZONE, VM_OPEN,
    VM_MAINS_FAIL, VM_MAINS_OK, VM_BAT_LOW, VM_ARM_REFUSED, VM_BAD_CODE,
    VM_LOCKOUT, VM_FAULT, VM_CHIME, VM_CALL_INTRO, VM_CALL_MENU, VM_ACK,
    VM_BOX_TAMPER, VM_PANIC_CALL,
    VM_COUNT
} voice_msg_t;

/* Identifiants de fragments : 0x0000-0x00FF messages, 0x1000+n nombres,
 * 0x2000+n noms de zones. */
#define VOICE_NUM(n)    (uint16_t)(0x1000u + (n))
#define VOICE_ZNAME(n)  (uint16_t)(0x2000u + (n))
#define VOICE_MAX_SEQ   8u

/** Texte de chaque message (sert à générer les WAV par synthèse vocale). */
extern const char *const voice_texts[VM_COUNT];

/** Nom de fichier d'un fragment, ex. "/VOIX/M006.WAV". */
void voice_filename(uint16_t id, char *out, size_t len);

/**
 * Séquence à annoncer localement pour un événement.
 * @param local  vrai = haut-parleur de la centrale (les alarmes panique ne
 *               sont jamais annoncées localement).
 * @return nombre de fragments écrits dans seq.
 */
unsigned voice_for_event(const alarm_config_t *cfg, const alarm_event_t *ev, bool local,
                         uint16_t *seq, unsigned max);
#endif
