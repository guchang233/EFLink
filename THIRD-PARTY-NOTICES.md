# Third-party notices

EF Link and its reused local EF Link game/network code are distributed under
GNU Affero General Public License v3.0. The full license is in `LICENSE`.

* Better-Endfield ABI headers: Dr-hydra / Better-Endfield,
  https://github.com/Dr-hydra/Better-Endfield,
  local source commit `35216279f716b9a7b90bf565ec7e25e8999705b9`, AGPL-3.0.
* Dear ImGui 1.91.9b: Copyright (c) 2014-2025 Omar Cornut,
  https://github.com/ocornut/imgui, MIT. Full license: `vendor/imgui/LICENSE.txt`
  in source or `licenses/imgui.txt` in the runtime archive.
  ImGui's bundled stb-derived files retain their upstream notices.
* JSON for Modern C++ 3.12.0: Copyright (c) 2013-2025 Niels Lohmann,
  https://github.com/nlohmann/json, MIT. Full license:
  `vendor/nlohmann-LICENSE.MIT` or `licenses/nlohmann.txt`.
* Architectural reference only: Grasscutters / Grasscutter,
  https://github.com/Grasscutters/Grasscutter,
  commit `f373827a83d3e688fd3c6afb1e1c3759ca1d61c9`, AGPL-3.0.
  No Grasscutter source code, game data or binaries are shipped with EF Link.
* Design reference only: honxi1 / Endfield-Poser,
  https://github.com/honxi1/Endfield-Poser,
  inspected commit `2b9e6a2a1cf9c4a78063352dbcf0c955bba685e5`, AGPL-3.0.
  Squad access, character-switch threading and restoration were reviewed.
  No Poser source, binaries or assets are included in EF Link distributions.
  Scope and findings are recorded in `docs/REFERENCES.md`.

The release contains no game assets. Character assets are loaded from the
player's installed game at runtime. The Microsoft YaHei font is loaded from
the local Windows fonts directory and is not redistributed.
