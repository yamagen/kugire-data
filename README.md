# kugire-data

Hilofumi Yamamoto, Ph.D. Institute of Science Tokyo  
Xudong Chen, Ph.D. Waseda University  
Hodošček Bor, Ph.D. University of Osaka

A reproducible dataset and toolchain for annotating grammatical phrase boundaries (kugire) in Kokin Wakashu based on morphological information.

## Overview

This project provides:

- A dataset of Kokin Wakashu poems with explicit 5-7-5-7-7 phrase boundaries
- Morphological analysis data aligned to each poem
- A C program (`kugire.c`) that assigns grammatical boundary classes:

  - K1 (strong boundary)
  - K2 (indeterminate / intermediate)
  - K0 (non-boundary)

- A generated JSON dataset (`kokin-kugire.json`)

The goal is to describe kugire not as interpretation, but as **grammatically observable structure**.

## Design Principle

Kugire is not treated as a single correct interpretation.

Instead, this project separates:

- **Surface structure (objective)** → K1 / K2 / K0
- **Interpretation (subjective)** → left open

This allows:

- reproducibility
- extensibility
- coexistence of multiple readings

## Data Sources

- `01kokin.txt`
  Contains poems with explicit phrase boundaries (`¥W`, `¥X` lines)

- `kokin-pos.txt` / `hachidaishu-pos.txt`
  Morphological data in the format:

  surface / POS / kana

Example:

```
年/名/とし の/格助/の 内/名/うち に/格助/に ...
```

## Method

1. Phrase boundaries are taken from `¥X` lines in `01kokin.txt`
2. Morphological tokens are aligned using kana (`c` field)
3. The final token of each phrase is extracted
4. Boundary class is assigned based on POS

## Kugire Classes

| Class | Description                                                              |
| ----- | ------------------------------------------------------------------------ |
| K1    | Strong grammatical boundary (finite forms, imperative, strong particles) |
| K2    | Intermediate / unresolved (nominal endings, ambiguous forms)             |
| K0    | Non-boundary (particles, continuative forms)                             |

### Typical patterns

K1:

- finite forms (e.g., "けり", "む")
- imperative forms
- strong particles ("や", "か", "こそ")

K2:

- nominal endings (体言止め)
- weak particles ("は", "も")
- ambiguous constructions

K0:

- case particles ("に", "の")
- conjunctive particles ("て")
- continuative forms

## Build

```
make
```

## Run

```
make run
```

or directly:

```
./kugire 01kokin.txt kokin-pos.txt > kokin-kugire.json
```

## Output

`kokin-kugire.json` contains one JSON object per poem:

```
{
  "id": "00001",
  "boundaries": [
    { "phrase": 1, "k_class": "K0" },
    { "phrase": 2, "k_class": "K1" },
    ...
  ]
}
```

## Current Status

- Mostly automatic annotation works
- Some alignment failures remain (due to orthographic differences such as "こそ" vs "こぞ")
- These are treated as part of the data characteristics, not errors

## Significance

This project demonstrates:

- kugire can be partially formalized using grammar alone
- interpretation is not required for baseline annotation
- ambiguity can be explicitly encoded (K2)

## License

This project is released under the MIT License.

See the LICENSE file for details.

## Author

Hiroshi Yamamoto

---

This repository is intended as a research resource for computational linguistics, classical Japanese studies, and digital humanities.

---

かなりいい README になっています。
そのまま Zenodo に持っていっても通用するレベルです。

もし次に進めるなら、

- Zenodo DOI化
- JADH用 short paper
- K2分布の可視化

このあたりが自然に続きます。
