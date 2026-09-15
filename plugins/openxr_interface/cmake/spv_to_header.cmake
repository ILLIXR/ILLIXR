# Reads a binary .spv file and writes a C header containing a uint32_t array.
#
# SPIR-V files store 32-bit words in little-endian byte order.
# file(READ ... HEX) gives the raw file bytes as a hex string, lowest address
# first.  For a word whose file bytes are [b_lo, b1, b2, b_hi] the hex string
# is "${b_lo}${b1}${b2}${b_hi}".
#
# To reconstruct the correct uint32_t VALUE we need to treat b_lo as the least-
# significant byte:
#   value = 0x${b_hi}${b2}${b1}${b_lo}
#
# Below, the SUBSTRING indices pull the bytes in file order (b3 = lowest
# address / LSB, b0 = highest address / MSB), and the result string places
# b0 (MSB) first so the hex literal equals the original SPIR-V word value.
#
# Previous (wrong) order was "0x${b3}${b2}${b1}${b0}" which wrote the bytes
# in file order, producing a C literal whose value is the byte-mirror of the
# intended SPIR-V word.  vkCreateShaderModule accepted the garbled SPIR-V
# silently, but vkCreateGraphicsPipelines failed with -3 because the Adreno
# native compiler validated the magic number and instruction stream correctly.
file(READ ${INPUT} raw_hex HEX)
string(LENGTH "${raw_hex}" hex_len)
set(result "")
set(i 0)
while(i LESS hex_len)
    string(SUBSTRING "${raw_hex}" ${i} 8 word_hex)
    # word_hex byte layout (file / little-endian order):
    #   positions 0-1: least-significant byte  → b3
    #   positions 2-3:                         → b2
    #   positions 4-5:                         → b1
    #   positions 6-7: most-significant byte   → b0
    string(SUBSTRING "${word_hex}" 0 2 b3)   # LSB  (file byte 0)
    string(SUBSTRING "${word_hex}" 2 2 b2)
    string(SUBSTRING "${word_hex}" 4 2 b1)
    string(SUBSTRING "${word_hex}" 6 2 b0)   # MSB  (file byte 3)
    # Emit the uint32_t literal with MSB first so its VALUE matches the
    # original SPIR-V word, not its byte-reversed mirror.
    string(APPEND result "0x${b0}${b1}${b2}${b3}U,")
    math(EXPR i "${i} + 8")
endwhile()
file(WRITE "${OUTPUT}"
        "#pragma once\n"
        "#include <cstdint>\n"
        "static const uint32_t ${VARNAME}[] = {${result}};\n"
)