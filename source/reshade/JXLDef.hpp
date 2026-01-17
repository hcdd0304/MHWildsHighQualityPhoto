/** Built-in primaries for color encoding. When decoding, the primaries can be
 * read from the @ref JxlColorEncoding primaries_red_xy, primaries_green_xy and
 * primaries_blue_xy fields regardless of the enum value. When encoding, the
 * enum values except ::JXL_PRIMARIES_CUSTOM override the numerical fields.
 * Some enum values match a subset of CICP (Rec. ITU-T H.273 | ISO/IEC
 * 23091-2:2019(E)), however the white point and RGB primaries are separate
 * enums here.
 */
typedef enum {
  /** The CIE xy values of the red, green and blue primaries are: 0.639998686,
     0.330010138; 0.300003784, 0.600003357; 0.150002046, 0.059997204 */
  JXL_PRIMARIES_SRGB = 1,
  /** Primaries must be read from the @ref JxlColorEncoding primaries_red_xy,
   * primaries_green_xy and primaries_blue_xy fields, or as ICC profile. This
   * enum value is not an exact match of the corresponding CICP value. */
  JXL_PRIMARIES_CUSTOM = 2,
  /** As specified in Rec. ITU-R BT.2100-1 */
  JXL_PRIMARIES_2100 = 9,
  /** As specified in SMPTE RP 431-2 */
  JXL_PRIMARIES_P3 = 11,
} JxlPrimaries;

/** Built-in transfer functions for color encoding. Enum values match a subset
 * of CICP (Rec. ITU-T H.273 | ISO/IEC 23091-2:2019(E)) unless specified
 * otherwise. */
typedef enum {
  /** As specified in ITU-R BT.709-6 */
  JXL_TRANSFER_FUNCTION_709 = 1,
  /** None of the other table entries describe the transfer function. */
  JXL_TRANSFER_FUNCTION_UNKNOWN = 2,
  /** The gamma exponent is 1 */
  JXL_TRANSFER_FUNCTION_LINEAR = 8,
  /** As specified in IEC 61966-2-1 sRGB */
  JXL_TRANSFER_FUNCTION_SRGB = 13,
  /** As specified in SMPTE ST 2084 */
  JXL_TRANSFER_FUNCTION_PQ = 16,
  /** As specified in SMPTE ST 428-1 */
  JXL_TRANSFER_FUNCTION_DCI = 17,
  /** As specified in Rec. ITU-R BT.2100-1 (HLG) */
  JXL_TRANSFER_FUNCTION_HLG = 18,
  /** Transfer function follows power law given by the gamma value in @ref
     JxlColorEncoding. Not a CICP value. */
  JXL_TRANSFER_FUNCTION_GAMMA = 65535,
} JxlTransferFunction;