# Known OpenGD77 hotspot-mode bugs and limitations

Found while using an OpenGD77 radio as a roaming DMR transceiver driven over
USB with the MMDVM serial protocol, by RadioDesk's `roaming_txvr`
(`~/dev2/CdsScoringTech/RadioDesk/roaming_txvr`; bench notes in its
README).

- **Hardware:** DM-1701.
- **Firmware:** `OpenGD77_HS v0.1.18 GitID #UNKNOWN (Radio:DM-1701, Mode:MMDVM)`, built
  from `OpenDM1701_202601_WithOutOfBandPatch.bin`.
- **Code:** references below are to the MK22 tree, `firmware/source/hotspot/uiHotspot.c`, unless
  noted. The DM-1701 builds from `~/dev2/OpenGD77-MDUV380` (branch `dmr-packet-data`), where the hotspot is
  `MDUV380_firmware/application/source/functions/hotspot.c`. The fixes below are in both trees.

Each entry is marked:
- **observed:** seen on the hardware;
- **source:** read in the code, not yet reproduced.

Each also says how it was seen or where it is. How to reproduce on the bench:
- drive the radio with `roaming_txvr` or a short script on its `mmdvm.Modem`
  class;
- watch the air with a DMR SDR receiver (`radiodesk-sdr`) on the same
  frequency.


## Status on build `7df0f50` (DM-1701 release, UDP, 2026-10-06)

Measured the same way as below: a script driving the radio over UDP and the SDR on 461.6875 MHz, with calls of 10
voice superframes, 6 bursts written up front, then paced at 60 ms. Image:
`~/dev2/OpenGD77/OpenDM1701_20261006_HotspotFixes_Trellis.bin`.

| # | Status |
|---|---|
| 1 | **Fixed** (see the table below). |
| 2 | **Not seen in 60 calls** across the last three images, every one decoded from burst A of the first superframe. Not proven fixed: the likeliest cause was the cut endings of item 3 (the "terminator right after the headers" looks like the rest of the previous call), and those are gone. Worth one more 12 call run with the original script. |
| 3 | **Fixed.** 24 of 24 calls "start" then "end", 3.78 s on air, including 12 that retuned the instant the transmitting bit cleared. The bit clears 0.70–0.73 s after the host writes the terminator (the buffered voice, the end of the superframe and the terminator), never before the terminator is on air, and it never dips. The roamer's 0.5 s wait can go. |
| 4 | Not seen over UDP (above). |
| 5 | Data bursts are passed up (CSBK, MBC, headers, and Rate 1/2, Rate 3/4 and Rate 1 blocks). Voice is still rebuilt from the decoded LC and AMBE (no talker alias, no real BER). |
| 6 | **Fixed (digital)** (above). Analog not tested yet. |
| 12 | **Fixed**, verified on air with the case above (preambles announcing 2 and 1, a header announcing 6 blocks): all 9 bursts in one transmission, written all at once and paced at 60 ms. |
| 13 | **Fixed.** Frames that arrive while a list is on air fill the rest of the 40 burst array and go out next. PI header, Idle and USBD are NAKed (4), a full list too (5). Rate 3/4 and Rate 1 blocks are now taken (Trellis and Rate 1 coding, host tested and cross-checked with RadioDesk's decoder; on air 2026-10-06: packets built by RadioDesk's encoder went out bit for bit, all at once and paced, and Rate 3/4 frames with 1-2 bit errors per block went out corrected, CRC-32 good). |
| 14 | **Fixed.** GET_STATUS shows transmitting while data is queued or on air; the DMR space includes the data list. |
| 15 | **Mitigated, cause still open** (`67791e6`). After a power on in USB network mode macOS sometimes never activates the link (a race: sometimes the device is configured but the data interface never selected, sometimes it isn't configured at all; once a power cycle was needed). The radio now re-enumerates up to 6 times, 3 s apart, also when it was never configured; the retries are given back once the link is up. 5 power cycles on 2026-10-06: all got the link without help in 2–10 s, 2 of them after one re-enumeration. To find the cause: a USB capture of a failing power on. |
| 16 | **New, fixed** (`7a33ed0` / `9fad228`). MMDVM over UDP: 11 data frames written back to back got 10 ACKs and the last block never went out; 60 ms apart they were fine. Cause: macOS packs back to back datagrams into one NCM transfer block (NTB), and the radio's guard against looping NDP chains counted datagrams, so everything after the eighth in a block was thrown away. It now counts NDPs. Before: 16 MMDVM requests at once got 14 replies, 30 got 28. After: 5, 9, 16, 30 and 40 all answered, and a 14-frame TMS written at once went out whole 3 of 3 (on air, CRC good, in RadioDesk). |

Root causes and fixes:
- **1:** the hotspot ignored the host's terminator and kept sending silence through a 360 ms network timeout and a
  720 ms stop delay; the HR-C6000 also sent 6 silence frames (360 ms) before the first voice frame. The terminator
  now ends the call once the buffer is empty (360 ms without voice if there is none), and the startup silence is
  gone. A call shorter than the buffering minimum starts on its terminator.
- **3:** at the end of a call the hotspot called `trxDisableTransmission()` while the HR-C6000 still had the rest of
  the superframe and the terminator to send. On the STM32 radios `trxIsTransmitting` follows the PA, which is
  switched burst by burst, and `radioSetRx()` cleared it between our bursts: GET_STATUS read idle for one poll, and
  the RF went to receive. It also stopped while the last voice frame was still waiting in the HR-C6000, which then
  sent silence instead. Now the hotspot waits for that frame, only clears `trxTransmissionEnabled` (as a PTT release
  does), and switches to receive once the HR-C6000 is idle. GET_STATUS takes transmitting from the HR-C6000 slot
  state (`HRC6000IsTransmitting()`), which the PA switching can't disturb. Note: `TX_END_3` is the terminator's slot
  itself; treating it as finished cut every call (tried and reverted).
- **12–14:** the data queue is now one module shared by both trees, `source/hotspot/hotspotData.c` (host tested in
  `firmware/tests/hotspotDataTest.c`). The data header always sets how many bursts belong to the list.

Commits: MK22 tree `2da0db4`, `4e581bf`, `f9e03fb`, `35b4278` (and the revert `9e18a58`); DM-1701 tree `c317c3c`,
`ef439cc`, `03163fb`, `49b63a8`, `4c91491`, `7df0f50`.

## Status on build `c317c3c` (UDP, 2026-10-06)

Measured with a script driving the radio over UDP (no HBlink4) and the SDR on
461.6875 MHz. Each call is 10 voice superframes (3.6 s of audio), 6 bursts
written up front, then paced at 60 ms. Clocks aligned to ±3 ms.

| # | Status |
|---|---|
| 1 | **Fixed.** The SDR hears the call 0.05–0.07 s after the first write. On air = audio + 0.18 s (the 3 headers), not +1.2 s. |
| 2 | **Still there, intermittent (2 calls in 12).** After 2–3 headers, about 0.42 s of nothing, and the whole first voice superframe (0.36 s of audio) is lost. The SDR logs "lost" then "late entry". In one of the two, the radio sent a terminator right after the headers, then carried on with the voice. Not tied to the previous call being cut off (calls right after a cut started clean). |
| 3 | **Not fixed.** The transmitting bit clears about 0.14 s before the terminator is on air. Retuning on the bit cuts the last 2 voice bursts and the terminator (6 calls of 6 end "lost", missing their last bursts). Waiting 1 s first: 60/60 and "end" (6 calls of 6). |
| 4 | **Not seen over UDP.** No missed status replies in 12 calls and 2 receive tests, including 20 bursts written at once. |
| 6 | **Fixed (digital).** Ch01 (MMDVM_HS) sent a CC2 call on 467.375 MHz. With the radio on CC1: carrier high for the whole call, nothing decoded. On CC2: the same, plus 62 bursts decoded. Carrier rose about 1.4 s before the first decoded burst and fell about 0.17 s after the last. Analog not tested yet. |
| 12–14 | Fixed per the agent that made the change (not retested here). |

Startup audio: on the 10 good starts all 60 voice bursts arrived in order,
starting with burst A of the first superframe, so nothing is clipped at the
burst level. Nobody has listened to it yet.

---

## 1. About 1.2 s of lead-in before the voice on every transmission (observed)

**What happens.** Every network call goes out on air about 1.2 s longer than its audio:

| Bursts sent (paced at 60 ms) | Audio | Time on air (SDR) |
|---|---|---|
| 20 | 1.2 s | 2.3 s |
| 62 | 3.7 s | 4.9 s |
| 74 | 4.4 s | 5.9 s |

GET_STATUS shows the DMR buffer **not draining at all for 1.2–1.4 s** after
the radio reports transmitting. The space stays put while frames keep
arriving. For a radio called by an automated assistant, that's 1.2 s of
dead air before every reply.

**Where to look.** The hand-off from `HOTSPOT_STATE_TX_START_BUFFERING` to
`HOTSPOT_STATE_TRANSMITTING` in `hotspotStateMachine()`, and what
`trxEnableTransmission()` and the HR-C6000 do before the first voice frame.
- `tx_delay` from SET_CONFIG was 100 ms, so it isn't that.
- Possibly LC headers repeated until some C6000 condition is met.

**Expected.** Voice should start about TX delay plus one or two headers after
keying, as on MMDVM_HS. On an MMDVM_HS_Dual_Hat in the same test, a 3.6 s call
was 4.1 s on air, and that includes 8 headers.

## 2. Sometimes a gap of about 0.75 s between the voice header and the voice (observed, intermittent)

**What happens.** One run in three: the SDR decoded the voice LC header, then
nothing decodable for about 0.75 s, then the voice. It logged that as
"start", "lost" (no voice within 0.5 s), then "late entry". The other two runs
of the same test decoded cleanly, so this probably depends on what the radio
sends during the lead-in in item 1.

**Repro.** Feed a 10-superframe call at a 60 ms pace with 6, 12 and 20 bursts
written up front, and watch the SDR. The 12-burst run showed it; the others
didn't. It also showed up once through HBlink4 with a 6-burst head start.

## 3. The terminator sent by the host is ignored, and "transmitting" clears before the radio's own end-of-call is out (observed + source)

**Source.** `storeNetFrame()` returns early for any burst whose sync is
`END_FRAME_PATTERN` (terminator) or `START_FRAME_PATTERN`. The transmission
ends only when the buffer runs empty: `HOTSPOT_STATE_TRANSMITTING` →
`HOTSPOT_STATE_TX_SHUTDOWN` when `wavbuffer_count == 0`.

**Observed.** GET_STATUS's transmitting bit (`getStatus()`, `buf[5] & 0x01`)
goes to 0 while the radio is still sending the end of the call:
- if the host retunes (SET_FREQ) as soon as the bit clears, the SDR loses the
  end of the call and logs "lost" instead of "end";
- waiting 0.5 s after the bit clears fixed it.

**Expected.** Either honor the terminator the host sends, or keep reporting
"transmitting" until the radio's final burst (and the terminator) is
actually on air.

## 4. GET_STATUS replies are sometimes lost just as the radio starts transmitting (observed)

**What happens.** With several DMR_DATA2 frames written back to back, the next
GET_STATUS sometimes gets **no reply** within 300 ms–2 s. It's the first poll
after keying up, and later polls answer in about 2 ms.
- 20 frames written in one go, then status: no reply within 2 s.
- 6 frames, then status every 100 ms: the first poll got no reply, the rest
  were fine.
- Paced feeding at 60 ms: replies every time.

**Where to look.**
- `enqueueUSBData()` / `processUSBDataQueue()`: the USB send ring. Each
  DMR_DATA2 queues an ACK, and the queue may overflow or wrap.
- The `0xFF` wrap marker logic (`usbComSendBuf`) at the moment
  `trxEnableTransmission()` runs.

**Workaround** in roaming_txvr: tolerate missed status replies for up to 2 s
while transmitting.

## 5. Only voice is passed up when receiving (source; by design, but limiting)

The RX path only builds voice LC headers, voice bursts and terminators
(`sendVoiceHeaderLC_Frame()`, `hotspotSendVoiceFrame()`,
`sendTerminator_LC_Frame()`). It rebuilds them from its own decoded LC and AMBE
rather than passing the received bursts up. So the host never sees:
- data headers and blocks (ARS registrations, GPS location reports);
- CSBKs (preambles, call alerts, radio checks);
- the talker alias: embedded data in bursts B–E is replaced with the LC
  (`DMREmbeddedData_getData()`);
- the radio's real BER: the bursts are re-encoded, so every received burst
  looks error-free.

MMDVM_HS and MMDVM firmware pass up every burst as heard.

**Update (fb791b7):** data bursts received on RF (CSBKs, headers, Rate 1/2 blocks) are now
passed up as DMR data frames. Not yet tested on air from this side.

## 6. No carrier detect in GET_STATUS (source)

`getStatus()` never sets the carrier-detect flag (`0x40` in the status
flags, which MMDVMHost reads as `m_cd`). A host can only listen before
talking by watching for decoded DMR frames (DMR_DATA2 / DMR_LOST2), so it
misses analog traffic, traffic on another color code, and noise. The AT1846S
RSSI (`trxRxSignal`) could drive it.

## 7. SET_CONFIG ignores most of what it's sent (source)

`setConfig()` reads TX delay, mode and color code, and nothing else.
- The DMR TX level (deviation) and CW ID level are ignored (the "To Do" block).
- The simplex/duplex flag is ignored.
- It requires only 13 bytes, where MMDVMHost protocol 1 sends 23.

Mostly harmless, but a host can't adjust deviation the way it can on other
MMDVM modems.

## 8. DMR_START / DMR_ABORT are ACKed but do nothing; DMR_DATA1 is NAKed (source; by design)

Simplex only, slot 2 only. This is fine for simplex use, but it should be
documented, since an MMDVMHost set to `Duplex=1` will fail confusingly.

## 9. SET_FREQ always resets the hotspot state (source; a caution)

`setFreq()` sets `hotspotState = HOTSPOT_STATE_INITIALISE`, which clears the
TX and RX buffers. Retuning in the middle of a transmission or reception
silently drops it. The host has to make sure the radio is idle first, and
because of item 3 that isn't straightforward.

## 10. Power steps from the RF level (source; a caution)

`setFreq()` maps the RF level byte to a power step:
- below 50: `rf_power / 12`;
- otherwise: `(rf_power / 50) + 3`;
- 255 means "use the menu power".

The steps are uneven and undocumented:

| RF level | Power step |
|---|---|
| 0–47 | 0–3 (12 values per step) |
| 48–99 | 4 |
| 100–149 | 5 |
| 150–199 | 6 |
| 200–254 | 7–8 |

A host can't ask for a power in watts.

## 11. Hotspot mode over UDP (done in 33a2fc0; tested)

Hotspot mode answers the MMDVM frames on UDP 3334 at the radio's address and
replies from 3334, as MMDVMHost's `Protocol=udp` expects. Tested 2026-10-06
with build `GitID #"20ee78a"` on CDC-NCM (radio 11.47.82.93, Mac 11.47.82.92):
version, retune in about 35 ms, a voice call through HBlink4 decoded start to
end by the SDR (about 0.3 s from tune to on air), and a voice call received on
the idle channel passed up to the host. Same as over CDC-ACM.

## 12. A data transmission from the host is cut after the header (observed + source)

**Observed** (UDP, 461.6875 MHz, SDR watching): host sends 2 preamble CSBKs
(blocks to follow 2, then 1), an unconfirmed data header announcing 6 Rate 1/2
blocks, and the 6 blocks.
- All 9 written at once: only the 2 CSBKs and the header go on air.
- Paced at 60 ms: CSBK, CSBK, header, then about 0.3 s of nothing, then the
  last 3 blocks as a second transmission. Blocks 1-3 never go out.

**Source** (`hotspotQueueNetData()` in `uiHotspot.c`):
- After the preambles, `netDataExpected` is 3. At the header,
  `netDataExpected < netDataCount` is false (3 == 3), so the header's 6 blocks
  are never added. The list counts as complete at 3 bursts and starts sending.
  The header should always set `netDataExpected = netDataCount + blocks`,
  whatever the preambles said (or at least use `<=`).
- Frames that arrive while `netDataSending` is true are dropped (see 13).

## 13. Data frames the hotspot can't take are ACKed and dropped (source + observed)

`hotspotModeReceiveNetFrame()` ignores `hotspotQueueNetData()`'s result and
returns 0 (ACK) for every data frame. So all of these are ACKed and silently
lost, and the host can't tell:
- frames while a list is being sent (`netDataSending`);
- frames past `DMR_DATA_MAX_BURSTS` (40);
- Rate 3/4 and Rate 1 blocks (not supported yet);
- PI headers, Idle and USBD (ACKed but never transmitted).

They should be NAKed (MMDVM error code), or better, queued: a second burst
list sent after the first.

## 14. GET_STATUS doesn't report a data transmission (source + observed)

`getStatus()` sets the transmitting bit from the voice states and CW only, not
`netDataSending`. During the data test above, status never showed
transmitting. A host can't tell when the data has gone out, and a host that
retunes when the radio is idle (roaming_txvr; SET_FREQ resets the hotspot,
item 9) could cut the transmission off. The free space in the status reply
doesn't count the data list either.

---

## Not bugs, for whoever picks this up
- **Retune speed is good.** SET_FREQ + SET_CONFIG + SET_MODE take about 36 ms
  in all (each ACKed in about 16 ms; GET_STATUS in about 2 ms), and keying up
  0 ms after a retune decoded fine.
- **The band limits menu must be Off** for frequencies outside the amateur
  bands (`trxCheckFrequencyInAmateurBand()` in `functions/trx.c`).
  435–438 MHz is always refused.
