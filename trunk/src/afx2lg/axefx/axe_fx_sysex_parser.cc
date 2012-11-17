// Copyright (c) 2012, Tomas Gunnarsson
// All rights reserved.

#include "axefx/axe_fx_sysex_parser.h"

#include "axefx/blocks.h"
#include "axefx/sysex_types.h"

namespace axefx {

SysExParser::SysExParser() {
}

SysExParser::~SysExParser() {
}

void SysExParser::ParsePresetId(const PresetIdHeader& header,
                                int size,
                                Preset* preset) {
  ASSERT(header.function() == PRESET_ID);
  ASSERT(size == (sizeof(header) + kSysExTerminationByteCount));
  if (header.preset_number.ms == 0x7f && header.preset_number.ls == 0x0) {
    // This is a special case that means the preset is destined for (or comes
    // from) the edit buffer.  In this case, we just set the id to -1.
    // http://forum.fractalaudio.com/axe-fx-ii-discussion/58581-help-loading-presets-using-sysex-librarian.html#post732659
    preset->id = -1;
  } else {
    preset->id = header.preset_number.As16bit();
  }
  ASSERT(header.unknown.As16bit() == 0x10);  // <- not sure what this is.
}

void SysExParser::ParsePresetProperties(int* preset_chunk_id,
                                        const PresetProperty& header,
                                        int size,
                                        Preset* preset) {
  ASSERT(header.function() == PRESET_PROPERTY);

  const Fractal16bit* values = reinterpret_cast<const Fractal16bit*>(&header.function_id);
  const int value_count = (size - sizeof(FractalSysExHeader) - 1) / sizeof(values[0]);

  // As far as I've seen, property blocks always start with this 16bit/3byte ID.
  ASSERT(values[0].As16bit() == 0x2078);

  if (values[0].As16bit() == 0x2078 && values[1].b2 == 0x04) {
    const PresetName* preset_name =
        reinterpret_cast<const PresetName*>(&values[3]);
    ASSERT(reinterpret_cast<const byte*>(preset_name) + sizeof(PresetName)
           < reinterpret_cast<const byte*>(&header) + size);
    preset->name = preset_name->ToString();
#if defined(_DEBUG)
    // In v7, the second triplet is 02 04 00 (2.2)
    // In v9 beta, this changed to 04 04 00 (2.4).
    // Mabe this is a version number of some sorts?
    printf("Preset %i %hs - (version number? %i.%i (%i/%X))\n",
        preset->id, preset->name.c_str(),
        values[1].As16bit() >> 8, values[1].As16bit() & 0xff,
        values[1].As16bit(),
        values[1].As16bit());
#endif
  } else if (*preset_chunk_id == 2) {
    // Effect blocks can be enumerated twice.  First in the first
    // property sysex property value - and this is optional.
    // Secondly in the 2nd property.
    //
    // If enumerated in the first property value, each block will have an
    // 8 16bit value (8 * 3 bytes) entry.  Before the blocks, there is a
    // section of 4 values.  Probably has something to do with the grid,
    // which has 4 rows.
    printf("Value count: %i - val[0]=0x%X\n",
        value_count, values[1].As16bit());
#if 0
    const BlockState* state = reinterpret_cast<const BlockState*>(&values[5]);
    while (reinterpret_cast<const byte*>(state + 1) <
           reinterpret_cast<const byte*>(&header) + size) {
      if (state->block_id.As16bit()) {
        printf("block (%i/%X): %hs\n",
          state->block_id.As16bit(), state->block_id.As16bit(),
          GetBlockName(state->block_id.As16bit()));
      }
      ++state;
    }
#endif
    for (int i = 0; i <= value_count; ++i) {
      if (values[i].As16bit() && values[i].As16bit() != 2) {
        printf("block (%i/%X): %hs\n",
            values[i].As16bit(), values[i].As16bit(),
            GetBlockName(values[i].As16bit()));
      }
    }
  } else if (*preset_chunk_id == 3) {
    // When enumerated in the 2nd property sysex, all the block parameters
    // will be included, so each entry will be variable in length.
    // It's possible that values[1] will then include a hint as to how long
    // the parameter block is.
    printf("Value count: %i - val[0]=0x%X\n",
        value_count, values[1].As16bit());
    for (int i = 2; i < value_count; ++i) {
      if (values[i].b1 == 0x6A) {
        // Bypass and X/Y state are stored in values[i].b3.
        bool x = (values[i].b3 & 0x2) == 0;  // y = !x
        bool bypassed = (values[i].b3 & 0x1) != 0;

        const Fractal16bit& byp = values[i + 30];
        printf("  %i Found Amp1 type=%hs(byp=%i x=%i) (%i) %i (preset %i %hs)\n",
            i, GetAmpName(values[i+2].As16bit()),
            bypassed,
            x,
            values[i+2].As16bit(),
            values[i+1].As16bit(),
            preset->id, preset->name.c_str());
      } else if (values[i].As16bit() == 0x6B) {
        printf("  %i Found Amp2 type=%hs (%i) (preset %i %hs)\n",
            i, GetAmpName(values[i+2].As16bit()),
            values[i+2].As16bit(), preset->id, preset->name.c_str());
      }
    }
  }
}

void SysExParser::ParsePresetEpilogue(const FractalSysExHeader& header,
                                      int size,
                                      Preset* preset) {
#ifdef _DEBUG
  printf("=================================================================\n");
#endif
}

void SysExParser::ParseFractalSysEx(int* preset_chunk_id,
                                    const FractalSysExHeader& header,
                                    int size,
                                    Preset* preset) {
  ASSERT(size > 0);
  if (header.model() != AXE_FX_II) {
    fprintf(stderr, "Not an AxeFx2 SysEx: %i\n", header.model_id);
    return;
  }

  switch (header.function()) {
    case PRESET_ID:
      ParsePresetId(static_cast<const PresetIdHeader&>(header), size, preset);
      break;

    case PRESET_PROPERTY:
      ParsePresetProperties(preset_chunk_id,
          static_cast<const PresetProperty&>(header), size, preset);
      break;

    case PRESET_EPILOGUE:
      ParsePresetEpilogue(header, size, preset);
      if (!preset->name.empty()) {
        presets_.insert(std::make_pair(preset->id, *preset));
        preset->id = -1;
        preset->name;
      }
      // Reset the counter for parsing the next preset.
      *preset_chunk_id = -1;
      break;

    default:
      fprintf(stderr, "*** Unknown function id: %i", header.function_id);
      break;
  }
}

void SysExParser::ParseSingleSysEx(int* preset_chunk_id,
                                   const byte* sys_ex,
                                   int size,
                                   Preset* preset) {
  ASSERT(sys_ex[0] == kSysExStart);
  ASSERT(sys_ex[size - 1] == kSysExEnd);

  if (size < (sizeof(kFractalMidiId) + kSysExTerminationByteCount) ||
      memcmp(&sys_ex[1], &kFractalMidiId[0], sizeof(kFractalMidiId)) != 0) {
    fprintf(stderr, "Not a Fractal sysex.\n");
    return;
  }

  if (!VerifyChecksum(sys_ex, size)) {
    fprintf(stderr, "Invalid checksum.\n");
    return;
  }

  ParseFractalSysEx(preset_chunk_id,
      *reinterpret_cast<const FractalSysExHeader*>(sys_ex), size, preset);
}

void SysExParser::ParseSysExBuffer(const byte* begin, const byte* end) {
  const byte* sys_ex_begins = NULL;
  const byte* pos = begin;
  Preset preset;
  preset.id = -1;
  int preset_chunk_id = 0;
  while (pos < end) {
    if (pos[0] == kSysExStart) {
      ASSERT(!sys_ex_begins);
      sys_ex_begins = &pos[0];
    } else if (pos[0] == kSysExEnd) {
      ASSERT(sys_ex_begins);
      ParseSingleSysEx(&preset_chunk_id, sys_ex_begins,
                       (pos - sys_ex_begins) + 1, &preset);
      sys_ex_begins = NULL;
      ++preset_chunk_id;
    }
    ++pos;
  }
  ASSERT(!sys_ex_begins);
}

}  // namespace axefx