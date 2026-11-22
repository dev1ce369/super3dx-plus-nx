# Headline titles

The arcade's menu titles ("Select brand", "Card data confirmed (1/9)") are ready-made pictures, one per string, in a heavy sans with
a red halo. No game font draws that style, so the port's own titles (a new string in the top bar) are drawn by `tools/headline.py`
and baked into `menu.bin` by `tools/menupack.py`.

- `fonts/LibreFranklin[wght].ttf` + `OFL-librefranklin.txt`: the letters (Libre Franklin Bold, 700; SIL Open Font License 1.1).
  `fetch_font.sh` fetches it again from google/fonts.
- `../headline.py`: `render_headline(text)`, the halo model and its constants.
- `test_titles.py`: renders a list of titles to one PNG, to look at them on a dark grey.

## How the halo was found (dev/, not needed to build)

Run against a dump of the arcade's title sprites (`ARCADE_VIEW`, default `build/arcade-view`; the English build's
`data/sprite-us/menu_p_topbar_mes_*.png`):

- `dev/edt_profile.py`: opacity and colour of the real halo by distance from the white letters (all the titles agree: a tight red
  falling to white, nothing past 6 px).
- `dev/alpha_colour.py`: the finding that the halo's colour is tied to its opacity: green and blue = 255 - alpha.
- `dev/fitglow.py`: fits the letters' mask blurred (sigma), opacity gain * blur ** power, to the real alpha.
  (best fit: sigma 1.8, power 0.8, gain 550; `headline.py` uses 1.95 / 0.83 / 450, tuned by eye against the real ones.)
- `dev/fetch_candidates.sh` + `dev/compare_fonts.py <spacing> a|b`: the candidate fonts drawn in the glow next to the arcade's own
  titles (`HEADLINE_FONTS` the font folder, `HEADLINE_OUT` where the sheets go). `dev/real_titles.py` sheets the real titles.
