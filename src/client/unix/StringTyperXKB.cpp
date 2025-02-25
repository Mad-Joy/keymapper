
#include "StringTyperXKB.h"

#if defined(ENABLE_XKBCOMMON)
# include <xkbcommon/xkbcommon.h>
#endif

constexpr uint32_t to_code(const char* name) {
  return (name[0] << 24) | (name[1] << 16) | (name[2] << 8) | (name[3] << 0);
}

Key xkb_keyname_to_key(const char* name) {
  if (!name[0])
    return Key::none;

  const auto name_code = to_code(name);
  switch (name_code) {
    case to_code("ESC"): return Key::Escape;
    case to_code("AE01"): return Key::Digit1;
    case to_code("AE02"): return Key::Digit2;
    case to_code("AE03"): return Key::Digit3;
    case to_code("AE04"): return Key::Digit4;
    case to_code("AE05"): return Key::Digit5;
    case to_code("AE06"): return Key::Digit6;
    case to_code("AE07"): return Key::Digit7;
    case to_code("AE08"): return Key::Digit8;
    case to_code("AE09"): return Key::Digit9;
    case to_code("AE10"): return Key::Digit0;
    case to_code("AE11"): return Key::Minus;
    case to_code("AE12"): return Key::Equal;
    case to_code("BKSP"): return Key::Backspace;
    case to_code("TAB"):  return Key::Tab;
    case to_code("AD01"): return Key::Q;
    case to_code("AD02"): return Key::W;
    case to_code("AD03"): return Key::E;
    case to_code("AD04"): return Key::R;
    case to_code("AD05"): return Key::T;
    case to_code("AD06"): return Key::Y;
    case to_code("AD07"): return Key::U;
    case to_code("AD08"): return Key::I;
    case to_code("AD09"): return Key::O;
    case to_code("AD10"): return Key::P;
    case to_code("AD11"): return Key::BracketLeft;
    case to_code("AD12"): return Key::BracketRight;
    case to_code("AC01"): return Key::A;
    case to_code("AC02"): return Key::S;
    case to_code("AC03"): return Key::D;
    case to_code("AC04"): return Key::F;
    case to_code("AC05"): return Key::G;
    case to_code("AC06"): return Key::H;
    case to_code("AC07"): return Key::J;
    case to_code("AC08"): return Key::K;
    case to_code("AC09"): return Key::L;
    case to_code("AC10"): return Key::Semicolon;
    case to_code("AC11"): return Key::Quote;
    case to_code("AC12"): return Key::IntlRo; // untested
    case to_code("TLDE"): return Key::Backquote;
    case to_code("BKSL"): return Key::Backslash;
    case to_code("AB01"): return Key::Z;
    case to_code("AB02"): return Key::X;
    case to_code("AB03"): return Key::C;
    case to_code("AB04"): return Key::V;
    case to_code("AB05"): return Key::B;
    case to_code("AB06"): return Key::N;
    case to_code("AB07"): return Key::M;
    case to_code("AB08"): return Key::Comma;
    case to_code("AB09"): return Key::Period;
    case to_code("AB10"): return Key::Slash;
    case to_code("SPCE"): return Key::Space;
    case to_code("LSGT"): return Key::IntlBackslash;
    case to_code("RTRN"): return Key::Enter;
  }
  return Key::none;
}

StringTyper::Modifiers get_xkb_modifiers(uint32_t mask) {
  const auto ShiftMask = (1 << 0);
  const auto Mod1Mask  = (1 << 3);
  const auto Mod5Mask  = (1 << 7);
  auto modifiers = StringTyper::Modifiers{ };
  if (mask & ShiftMask) modifiers |= StringTyper::Shift;
  if (mask & Mod1Mask)  modifiers |= StringTyper::Alt;
  if (mask & Mod5Mask)  modifiers |= StringTyper::AltGr;
  return modifiers;
}

//-------------------------------------------------------------------------

bool StringTyperXKB::update_layout_xkbcommon(xkb_context* context, xkb_keymap* keymap) { 
#if defined(ENABLE_XKBCOMMON)
  if (!context || !keymap)
    return false;
  const auto min = xkb_keymap_min_keycode(keymap);
  const auto max = xkb_keymap_max_keycode(keymap);
  auto symbols = std::add_pointer_t<const xkb_keysym_t>{ };
  auto masks = std::array<xkb_mod_mask_t, 8>{ };

  m_dictionary.clear();
  for (auto keycode = min; keycode < max; ++keycode)
    if (auto name = xkb_keymap_key_get_name(keymap, keycode))
      if (auto key = xkb_keyname_to_key(name); key != Key::none) {
        const auto layouts = xkb_keymap_num_layouts_for_key(keymap, keycode);
        for (auto layout = 0u; layout < layouts; ++layout) {
          const auto levels = xkb_keymap_num_levels_for_key(keymap, keycode, layout);
          for (auto level = 0u; level < levels; ++level) {
            const auto num_symbols = xkb_keymap_key_get_syms_by_level(keymap, keycode,
              layout, level, &symbols);
            const auto num_masks = xkb_keymap_key_get_mods_for_level(keymap, keycode,
              layout, level, masks.data(), masks.size());
            if (num_symbols > 0 && num_masks > 0)
              if (auto character = xkb_keysym_to_utf32(symbols[0]))
                if (m_dictionary.find(character) == m_dictionary.end())
                  m_dictionary[character] = { key, get_xkb_modifiers(masks[0]) };
          }
        }
      }
  return true;
#else // !ENABLE_XKBCOMMON
  return false;
#endif
}
