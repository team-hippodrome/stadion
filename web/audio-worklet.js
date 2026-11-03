// Web build: plays the game's sound on the browser's audio thread (src/port/web_audio.cpp). The game fills a ring
// buffer of stereo float frames in its WebAssembly memory: two Int32 indices (write, read; in frames), then the frames.
// The memory and the ring's address arrive in a message once the game opens its sound output.
class PortOutput extends AudioWorkletProcessor {
  constructor() {
    super();
    this.port.onmessage = (e) => {
      const { buffer, base, frames } = e.data;
      this.index = new Int32Array(buffer, base, 2);
      this.data = new Float32Array(buffer, base + 8, frames * 2);
      this.mask = frames - 1;
    };
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const left = out[0], right = out[1] || out[0];
    if (!this.data) {
      return true;
    }
    let read = Atomics.load(this.index, 1);
    let avail = (Atomics.load(this.index, 0) - read) & this.mask;
    for (let i = 0; i < left.length; i++) {
      if (avail > 0) {
        left[i] = this.data[read * 2];
        right[i] = this.data[read * 2 + 1];
        read = (read + 1) & this.mask;
        avail--;
      } else {
        left[i] = right[i] = 0; // ran dry: silence until the game catches up
      }
    }
    Atomics.store(this.index, 1, read);
    return true;
  }
}

registerProcessor("port-output", PortOutput);
