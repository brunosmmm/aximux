//parameters
define register_size 32
define addr_mode byte

// generative parameters
param mux_signals 8
param alt_signals 1
param alt_signal_size $clog2(alt_signals)

//registers
template register SRCSEL description="Source select register" {
  // Source selection
  field SRC position={alt_signal_size-1}..0 access=RW description="Source select"
  // Short selection: shorts input to incoming signals (fanout beware)
  field SHORT position=5 access=RW description="Short input to incoming signal"
  // Direction control enable: control direction in HW (=0) or SW (=1)
  field DCTL position=6 access=RW description="SW direction control enable"
  // Software Direction control
  field DIR position=7 access=RW description="SW direction control"
}

// Selector registers
for ii in <0, mux_signals> generate {
  register SRCSEL{ii} description="Source select register {ii}" using SRCSEL

  output srcsel{ii} source=SRCSEL{ii}.SRC
  output shortsel{ii} source=SRCSEL{ii}.SHORT
  output diren{ii} source=SRCSEL{ii}.DCTL
  output dirctl{ii} source=SRCSEL{ii}.DIR
}

// Information Registers
register MUXINFO at 0x80 description="Hardware Information" {
  field SIGNALS position=7..0 access=R default={mux_signals} description="Number of muxes"
  field ALTSIGS position=15..8 access=R default={alt_signals} description="Number of sources for each mux"
  field RESERVED position=31..16 access=R description="Reserved"
}
