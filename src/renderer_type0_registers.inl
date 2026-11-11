  // Sequential packets can use the existing backend constant-range path.
  // Repeated-register packets must preserve every individual write/side effect.
  if (!write_one_reg && REXCVAR_GET(outrun_bulk_registers)) {
    WriteRegisterRangeFromRing(reader, base_index, count);
  } else {
    for (uint32_t m = 0; m < count; m++) {
      uint32_t reg_data = reader->ReadAndSwap<uint32_t>();
      uint32_t target_index = write_one_reg ? base_index : base_index + m;
      WriteRegister(target_index, reg_data);
    }
  }
