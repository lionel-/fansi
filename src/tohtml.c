/*
 * Copyright (C) 2018  Brodie Gaslam
 *
 * This file is part of "fansi - ANSI Control Sequence Aware String Functions"
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Go to <https://www.r-project.org/Licenses/GPL-2> for a copy of the license.
 */

#include "fansi.h"
/*
 * Looks like we don't need to worry about C0 sequences, however we must
 * parse all ESC sequences as HTML gladly displays everything right after the
 * initial ESC.
 *
 * In terms of sequences we consider meaningful, only colors and basic styles
 * for html translation
 */
struct FANSI_css {const char * css; int len;};

static const struct FANSI_css css_style[9] = {
  // Code 1: bold
  {.css="font-weight: bold;", .len=18},
  // Code 2: lighter
  {.css="font-weight: 100;", .len=17},
  // Code 3: italic
  {.css="font-style: italic;", .len=19},
  // Code 4: underline
  {.css="text-decoration: underline;", .len=27},
  // Code 5: blink
  {.css="text-decoration: blink;", .len=23},
  // Code 6: blink
  {.css="text-decoration: blink;", .len=23},
  // Code 7: invert; unused
  {.css="", .len=0},
  // Code 8: conceal
  {.css="color: transparent;", .len=19},
  // Code 9: line-through
  {.css="text-decoration: line-through;", .len=30},
};
/*
 * All color conversions taken from
 *
 * <https://en.wikipedia.org/wiki/ANSI_escape_code>
 *
 * @param color an integer expected to be between 0 and 9
 * @param color_extra a pointer to a 4 long integer array as you would get in
 *   struct FANSI_state.color_extra
 * @param buff must be pre-allocated to be able to hold the color in format
 *   #FFFFFF including the null terminator (so at least 8 bytes)
 * @return how many bytes were written, guaranteed to be 7 bytes, does not
 *   include the NULL terminator that is also written just in case.
 */

static int color_to_html(
  int color, int * color_extra, char * buff
) {
  // CAREFUL: DON'T WRITE MORE THAN 7 BYTES + NULL TERMINATOR

  const char * dectohex = "0123456789ABCDEF";
  const char * std_16[16] = {
    "000000", "800000", "008000", "808000",
    "000080", "800080", "008080", "C0C0C0",
    "808080", "FF0000", "00FF00", "FFFF00",
    "0000FF", "FF00FF", "00FFFF", "FFFFFF"
  };
  // According to <https://en.wikipedia.org/wiki/ANSI_escape_code> these are the
  // putty defaults

  const char * std_8[8] = {
    "000000", "BB0000", "00BB00", "BBBB00",
    "0000BB", "BB00BB", "00BBBB", "BBBBBB"
  };
  const char * std_5[6] = {"00", "5F", "87", "AF", "D7", "FF"};
  const char * bright[8] = {
    "555555", "FF5555", "55FF55", "FFFF55",
    "5555FF", "FF55FF", "55FFFF", "FFFFFF"
  };
  char * buff_track = buff;

  if(color == 9) {
    error(
      "Internal Error: should not be applying no-color; contact maintainer."
    );
  } else if(color >= 0 && color < 10) {
    *(buff_track++) = '#';

    if(color == 8) {
      if(color_extra[0] == 2) {
        for(int i = 1; i < 4; ++i) {
          char hi = dectohex[color_extra[i] / 16];
          char lo = dectohex[color_extra[i] % 16];
          *(buff_track++) = hi;
          *(buff_track++) = lo;
        }
      } else if(color_extra[0] == 5) {
        // These are the 0-255 color codes
        int color_5 = color_extra[1];
        if(color_5 < 0 || color_5 > 255)
          error("Internal Error: 0-255 color outside of that range."); // nocov
        if(color_5 < 16) {
          // Standard colors
          memcpy(buff_track, std_16[color_5], 6);
          buff_track += 6;
        } else if (color_5 < 232) {
          int c5 = color_5 - 16;
          int c5_r = c5 / 36;
          int c5_g = (c5 % 36) / 6;
          int c5_b = c5 % 6;

          if(c5_r > 5 || c5_g > 5 || c5_b > 5)
            error("Internal Error: out of bounds computing 6^3 clr."); // nocov

          memcpy(buff_track, std_5[c5_r], 2);
          buff_track += 2;
          memcpy(buff_track, std_5[c5_g], 2);
          buff_track += 2;
          memcpy(buff_track, std_5[c5_b], 2);
          buff_track += 2;
        } else {
          int c_bw = (color_5 - 232) * 10 + 8;
          char hi = dectohex[c_bw / 16];
          char lo = dectohex[c_bw % 16];
          for(int i = 0; i < 3; ++i) {
            *(buff_track++) = hi;
            *(buff_track++) = lo;
          }
        }
      } else
        // nocov start
        error(
          "Internal Error: invalid 256 or tru color code; contact maintainer."
        );
        // nocov end
    } else {
      memcpy(buff_track, std_8[color], 6);
      buff_track += 6;
    }
  } else if(
    (color >= 90 && color <= 97) ||
    (color >= 100 && color <= 107)
  ) {
    *(buff_track++) = '#';
    int c_bright = color >= 100 ? color - 100 : color - 90;
    memcpy(buff_track, bright[c_bright], 6);
    buff_track += 6;
  } else {
    error("Internal Error: invalid color code %d", color); // nocov
  }
  *buff_track = 0;
  return (int) (buff_track - buff);
}

static int state_as_html(struct FANSI_state state, int first, char * buff) {
  /****************************************************\
  | IMPORTANT: KEEP THIS ALIGNED WITH FANSI_csi_write  |
  | although right now ignoring rare escapes in html   |
  \****************************************************/

  // Styles
  const char * buff_start = buff;
  if(!FANSI_state_has_style_basic(state)) {
    if(first)
      // nocov start
      error("Internal Error: no state in first span; contact maintainer.");
      // nocov end
    if(state.string[state.pos_byte]) {
      memcpy(buff, "</span><span>", 13);
      buff += 13;
    }
  } else {
    if(first) {
      memcpy(buff, "<span style='", 13);
      buff += 13;
    } else {
      memcpy(buff, "</span><span style='", 20);
      buff += 20;
    }
    // Colors color: #FFFFFF; background-color: #FFFFFF;

    int invert = state.style & (1 << 7);
    int color = invert ? state.bg_color : state.color;
    int * color_extra = invert ? state.bg_color_extra : state.color_extra;
    int bg_color = invert ? state.color : state.bg_color;
    int * bg_color_extra = invert ? state.color_extra : state.bg_color_extra;

    if(color >= 0) {
      memcpy(buff, "color: ", 7);
      buff += 7;
      buff += color_to_html(color, color_extra, buff);
      *(buff++) = ';';
    }
    if(bg_color >= 0) {
      memcpy(buff, "background-color: ", 18);
      buff += 18;
      buff += color_to_html(bg_color, bg_color_extra, buff);
      *(buff++) = ';';
    }
    // Styles (need to go after color for transparent to work)

    for(int i = 1; i < 10; ++i) {
      if(state.style & (1 << i)) {
        memcpy(buff, css_style[i - 1].css, css_style[i - 1].len);
        buff += css_style[i - 1].len;
      }
    }
    *(buff++) = '\'';
    *(buff++) = '>';
    *buff = 0;
  }
  return (int)(buff - buff_start);
}
/*
 * Compute size of each state
 */
static int state_size_as_html(struct FANSI_state state, int first) {
  int size = 0;
  if(!FANSI_state_has_style_basic(state)) {
    if(first)
      // nocov start
      error("Internal Error: no state in first span; contact maintainer.");
      // nocov end

    // Only need to re-open tag if not at end of string
    if(state.string[state.pos_byte]) {
      size = 13;  // </span><span>
    }
  } else {
    if(first) {
      size = 15;  // <span style="">
    } else {
      size = 22;  // </span><span style="">
    }
    // Styles

    for(int i = 1; i < 10; ++i) {
      if(state.style & (1 << i)) size += css_style[i - 1].len;
    }
    // Colors color: #FFFFFF; background-color: #FFFFFF;

    int invert = state.style & (1 << 7);
    if(state.color >= 0) size += invert ? 26 : 15;
    if(state.bg_color >= 0) size += invert ? 15 : 26;
  }
  return size;
}
/*
 * Helper functions to process size and write the HTML split off
 * for clarity
 */

static int html_compute_size(
  struct FANSI_state state, int bytes_extra, int bytes_esc_start, int first,
  R_xlen_t i
) {
  // bytes_esc cannot overflow int because the input is supposed to be an
  // R sourced string

  int bytes_esc = state.pos_byte - bytes_esc_start;
  int bytes_html = state_size_as_html(state, first);
  int bytes_net = bytes_html - bytes_esc;

  if(bytes_net >= 0) {
    if(bytes_extra > FANSI_int_max - bytes_net) {
      error(
        "%s%s %.0f %s",
        "Expanding SGR sequences into CSS will create a string longer ",
        "than INT_MAX at position", (double) (i + 1),
        "which is not allowed by R."
      );
    }
    bytes_extra += bytes_net;
  } else {
    if(bytes_extra < FANSI_int_min - bytes_net) {
      // This should actually be impossible as a string that is only ESC
      // sequences with no SGR is the only way to get that big a decrease,
      // and if it doesn't have SGR then it wouldn't enter this loop
      // nocov start
      error(
        "%s%s",
        "Internal Error: unexpectedly large byte shrinking when ",
        "converting ESC sequences to CSS; contact maintainer."
      );
      // nocov end
    }
    bytes_extra += bytes_net;
  }
  return bytes_extra;
}
// Check for overall overflow, recall that R allows up to INT_MAX long strings
// excluding the NULL
//
// However, need size_t return since including the NULL terminator we could need
// over int size.

static size_t html_check_overflow(
  int bytes_extra, int bytes_init, int span_extra, R_xlen_t i
) {
  size_t bytes_final;
  if(bytes_init < 0) error("Internal error: bytes_init must be positive.");
  if(
    bytes_extra >= 0 && (
      bytes_init > FANSI_int_max - bytes_extra - span_extra
    )
  ) {
    error(
      "%s%s %.0f %s",
      "String with SGR sequences as CSS is longer ",
      "than INT_MAX at position", (double) (i + 1),
      "which is not allowed by R."
    );
  } else if(bytes_extra < 0) {
    if(bytes_extra <= FANSI_int_min + span_extra) {
      // nocov start
      error(
        "%s%s%s",
        "Internal error: integer overflow when trying to compute net ",
        "additional bytes requires by conversion of SGR to HTML. ",
        "Contact maintainer"
      );
      // nocov end
    }
    int bytes_extra_extra = bytes_extra + span_extra;

    if(bytes_init + bytes_extra_extra < 0)
      // nocov start
      error(
        "%s%s",
        "Internal Error: CSS would translate to negative length string; ",
        "this should not happen."
      );
      // nocov end
  }
  bytes_final = (size_t) bytes_init + bytes_extra + span_extra + 1;
  return bytes_final;
}
SEXP FANSI_esc_to_html(SEXP x, SEXP warn, SEXP term_cap) {
  if(TYPEOF(x) != STRSXP)
    error("Internal Error: `x` must be a character vector");  // nocov

  R_xlen_t x_len = XLENGTH(x);
  struct FANSI_buff buff = {.len=0};
  struct FANSI_state state, state_prev, state_init;
  state = state_prev = state_init = FANSI_state_init("", warn, term_cap);

  SEXP res = x;
  // Reserve spot on protection stack
  PROTECT_INDEX ipx;
  PROTECT_WITH_INDEX(res, &ipx);

  SEXP * x_ptr = STRING_PTR(x);

  for(R_xlen_t i = 0; i < x_len; ++i, ++x_ptr) {
    FANSI_interrupt(i);

    SEXP chrsxp = *x_ptr;
    FANSI_check_enc(chrsxp, i);

    const char * string_start = CHAR(chrsxp);
    const char * string = string_start;

    // Reset position info and string; we want to preserve the rest of the state
    // info so that SGR styles can spill across lines

    state = FANSI_reset_pos(state);
    state.string = string;
    struct FANSI_state state_start = FANSI_reset_pos(state);

    // Save what the state was at the end of the prior string

    R_len_t bytes_init = LENGTH(chrsxp);

    int bytes_extra = 0;   // Net bytes being add via tags (css - ESC)
    size_t bytes_final = 0;
    int has_esc, any_esc;
    has_esc = any_esc = 0;

    // Process the strings in two passes, in pass 1 we compute how many bytes
    // we'll need to store the string, and in the second we actually write it.
    // This is obviously a bit wasteful as we parse the ESC sequences twice, but
    // the alternative is to track a growing list or some such of accrued parsed
    // sequences.  The latter might be faster, but more work for us so we'll
    // leave it and see if it becomes a major issue.

    // It is possible for a state to be left over from prior string.

    if(FANSI_state_has_style_basic(state)) {
      bytes_extra = html_compute_size(
        state, bytes_extra, state.pos_byte, 0, i
      );
      has_esc = any_esc = 1;
    }
    state_prev = state;

    // Now check string proper

    while(*string && (string = strchr(string, 0x1b))) {
      if(!any_esc) any_esc = 1;

      // Since we don't care about width, etc, we only use the state objects to
      // parse the ESC sequences, so we don't have to worry about UTF8
      // conversions.

      state.pos_byte = (string - string_start);

      // read all sequential ESC tags and compute the net change in size to hold
      // them

      int esc_start = state.pos_byte;
      state = FANSI_read_next(state);
      if(FANSI_state_comp_basic(state, state_prev)) {
        bytes_extra =
          html_compute_size(state, bytes_extra, esc_start, !has_esc, i);
        if(!has_esc) has_esc = 1;
      }
      state_prev = state;
      ++string;
    }
    if(any_esc) {
      // we will use an extra <span></span> to simplify logic

      int span_end = has_esc * 7;

      bytes_final = html_check_overflow(bytes_extra, bytes_init, span_end, i);

      // Allocate target vector if it hasn't been yet

      if(res == x) REPROTECT(res = duplicate(x), ipx);

      // Allocate buffer and do second pass

      FANSI_size_buff(&buff, bytes_final);
      string = string_start;
      state_start.warn = state.warn;
      state = state_start;

      // Rprintf("string is '%s'\n", state.string);

      int first_esc = 1;
      char * buff_track = buff.buff;

      // Handle state left-over from previous char elem

      if(FANSI_state_has_style_basic(state)) {
        int bytes_html = state_as_html(state, first_esc, buff_track);
        buff_track += bytes_html;
        first_esc = 0;
      }
      state_prev = state;

      // Deal with state changes in this string

      while(*string && (string = strchr(string, 0x1b))) {
        state.pos_byte = (string - string_start);

        // read all sequential ESC tags

        state = FANSI_read_next(state);

        // The text since the last ESC

        // Rprintf("prev_byte: %d\n", state_prev.pos_byte);
        const char * string_last = string_start + state_prev.pos_byte;
        int bytes_prev = string - string_last;
        // Rprintf("bytes prev: %d\n", bytes_prev);
        // Rprintf("write prev: '%.*s'\n", bytes_prev, string_last);
        memcpy(buff_track, string_last, bytes_prev);
        buff_track += bytes_prev;

        // If we have a change from the previous tag, write html/css

        if(FANSI_state_comp_basic(state, state_prev)) {
          int bytes_html = state_as_html(state, first_esc, buff_track);
          // Rprintf("write html: '%.*s'\n", bytes_html, buff_track);
          buff_track += bytes_html;
          if(first_esc) first_esc = 0;
        }
        state_prev = state;
        string = state.string + state.pos_byte;
      }
      // Last hunk left to write and trailing SPAN

      const char * string_last = state_prev.string + state_prev.pos_byte;
      int bytes_stub = bytes_init - (string_last - string_start);
      // Rprintf("last: '%s'\n", string_last);
      // Rprintf("stub %d string %d\n", bytes_stub, (string_last - string_start));

      memcpy(buff_track, string_last, bytes_stub);
      buff_track += bytes_stub;

      if(has_esc) {
        // odd case where the only thing in the string is a null SGR
        memcpy(buff_track, "</span>", span_end);
        buff_track += span_end;
      }
      *(buff_track) = '0';  // not strictly needed

      // Now create the charsxp what encoding to use.

      if(buff_track - buff.buff > FANSI_int_max)
        // nocov start
        error(
          "%s%s",
          "Internal Error: attempting to write string longer than INT_MAX; ",
          "contact maintainer (3)."
        );
        // nocov end

      cetype_t chr_type = getCharCE(chrsxp);
      SEXP chrsxp = PROTECT(
        mkCharLenCE(buff.buff, (int) (buff_track - buff.buff), chr_type)
      );
      SET_STRING_ELT(res, i, chrsxp);
      UNPROTECT(1);
    }
  }
  UNPROTECT(1);
  return res;
}
/*
 * Testing interface
 *
 * x is a 5 x N matrix where, for each column the first value is a color code,
 * and subsequent values correspond to the state.color_extra values.
 */

SEXP FANSI_color_to_html_ext(SEXP x) {
  if(TYPEOF(x) != INTSXP)
    error("Argument must be integer.");  // nocov

  R_xlen_t len = XLENGTH(x);
  if(len % 5)
    error("Argument length not a multipe of 5"); // nocov

  struct FANSI_buff buff = {.len = 0};
  FANSI_size_buff(&buff, 8);

  int * x_int = INTEGER(x);

  SEXP res = PROTECT(allocVector(STRSXP, len / 5));

  for(R_xlen_t i = 0; i < len; i += 5) {
    int size = color_to_html(x_int[i], x_int + (i + 1), buff.buff);
    if(size < 1) error("Internal Error: size should be at least one");
    SEXP chrsxp = PROTECT(mkCharLenCE(buff.buff, size, CE_BYTES));
    SET_STRING_ELT(res, i / 5, chrsxp);
    UNPROTECT(1);
  }
  UNPROTECT(1);
  return res;
}


