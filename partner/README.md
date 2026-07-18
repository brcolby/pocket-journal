# Pocket Journal Partner

`pocket-journal-partner` installs the `pj` command used to provision, inspect,
sync, transcribe, update, and maintain a Pocket Journal over USB-C or a trusted
local network.

Install the release wheel in an isolated environment:

```sh
python3 -m pip install --user pipx
python3 -m pipx ensurepath
pipx install pocket_journal_partner-0.0.0-py3-none-any.whl
pj --help
```

Python 3.11 or newer is required. Bluetooth provisioning and the optional
Hugging Face transcription backend can be installed from a source checkout:

```sh
python3 -m pip install -e './partner[ble,transcription]'
```

Start with the repository's
[install and flashing guide](https://github.com/brcolby/pocket-journal/blob/main/docs/install.md),
then use the complete
[`pj` command reference](https://github.com/brcolby/pocket-journal/blob/main/docs/cli-reference.md)
and
[sync guide](https://github.com/brcolby/pocket-journal/blob/main/docs/partner-sync.md).

Pocket Journal Partner is released under the MIT License. See `LICENSE` in this
distribution.
