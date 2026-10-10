# Korean composition core

Development code, not connected to the shipping RIUM Keys application or Windows
TSF. Passing these tests does not establish application shortcut compatibility.

Run `./input-core/test.ps1` from the repository root. The standalone test covers
all 11,172 modern Korean syllables, word sequences, final-consonant migration,
compound-vowel/final deletion, and physical virtual-key handling. The current
suite contains 11,237 assertions.

`Hangul.h` implements two-beolsik composition. `TextInput.h` handles physical keys
only after a host has established an editable input context. Neither file detects
whether an arbitrary application is accepting text.

Host integration must:

- Preview test-key callbacks on a copy; mutate state only once per accepted key.
- Apply committed text before allowing navigation or punctuation through.
- Keep edits bound to the original text context and handle edit-session failure.
- Distinguish text contexts from shortcut surfaces before consuming letters.
- Preserve Korean mode across focus changes and respect explicit mode switching.

The TSF registration/activation fixture remains a separate release gate. This
module must not be advertised as a completed Windows-wide input method.
