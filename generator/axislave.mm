//parameters
#register_size 32;
#addr_mode byte;

// generative parameters
param mux_signals 8;
param alt_signals 1;
param alt_signal_size $clog2(alt_signals);

//registers

// Selector registers
for ii in <0, mux_signals> generate
  register SRCSEL{ii} description="Source select register {ii}";

  // Source selection
  field SRCSEL{ii}.SRC position={alt_signal_size-1}..0 access=RW;
  // Short selection: shorts input to incoming signals (fanout beware)
  field SRCSEL{ii}.SHORT position=5 access=RW;
  // Direction control enable: control direction in HW (=0) or SW (=1)
  field SRCSEL{ii}.DCTL position=6 access=RW;
  // Software Direction control
  field SRCSEL{ii}.DIR position=7 access=RW;

  output srcsel{ii} source=SRCSEL{ii}.SRC;
  output shortsel{ii} source=SRCSEL{ii}.SHORT;
  output diren{ii} source=SRCSEL{ii}.DCTL;
  output dirctl{ii} source=SRCSEL{ii}.DIR;
end;

// Information Registers
register MUXINFO at 0x80 description="Hardware Information";
field MUXINFO.SIGNALS position=7..0 access=R default={mux_signals};
field MUXINFO.ALTSIGS position=15..8 access=R default={alt_signals};
field MUXINFO.RESERVED position=31..16 access=R;
