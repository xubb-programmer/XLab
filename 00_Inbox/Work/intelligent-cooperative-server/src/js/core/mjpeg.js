const MAX_HEADER_BYTES = 1024;
const MAX_FRAME_BYTES = 4 * 1024 * 1024;
const textDecoder = new TextDecoder();

function headerEnd(bytes) {
  for (let i = 0; i < Math.min(bytes.length - 3, MAX_HEADER_BYTES); i++) {
    if (bytes[i] === 13 && bytes[i + 1] === 10 && bytes[i + 2] === 13 && bytes[i + 3] === 10) return i;
  }
  return -1;
}

export async function* readJpegFrames(body) {
  const reader = body.getReader();
  let pending = new Uint8Array(0);
  let frameBytes = 0;

  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) return;

      if (pending.length) {
        const joined = new Uint8Array(pending.length + value.length);
        joined.set(pending);
        joined.set(value, pending.length);
        pending = joined;
      } else {
        pending = value;
      }

      while (pending.length) {
        if (!frameBytes) {
          const end = headerEnd(pending);
          if (end < 0) {
            if (pending.length >= MAX_HEADER_BYTES + 3) throw new Error("Invalid video frame header");
            break;
          }
          const header = textDecoder.decode(pending.subarray(0, end));
          frameBytes = Number(/^Content-Length:[ \t]*(\d+)[ \t]*\r?$/im.exec(header)?.[1]);
          if (!Number.isInteger(frameBytes) || frameBytes < 1 || frameBytes > MAX_FRAME_BYTES) {
            throw new Error("Invalid video frame size");
          }
          pending = pending.subarray(end + 4);
        }

        if (pending.length < frameBytes) break;
        yield new Blob([pending.subarray(0, frameBytes)], { type: "image/jpeg" });
        pending = pending.subarray(frameBytes);
        frameBytes = 0;
      }
    }
  } finally {
    await reader.cancel().catch(() => {});
    reader.releaseLock();
  }
}
