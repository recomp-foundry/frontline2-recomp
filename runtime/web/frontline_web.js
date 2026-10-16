// The web build's JavaScript library (--js-library): the disc as a WasmFS backend
// whose reads are asynchronous (fetch, or a file the user picked) and awaited
// with Asyncify, so the game can read it from the page's main thread.
// Module.frontlineDisc (frontline_disc.js) provides list() and read().
addToLibrary({
  frontline_disc_backend__deps: ['$wasmFS$backends'],
  frontline_disc_backend__sig: 'vp',
  frontline_disc_backend: (backend) => {
    const files = new Map();
    wasmFS$backends[backend] = {
      allocFile: (file) => { files.set(file, Module.frontlinePending); },
      freeFile: (file) => { files.delete(file); },
      write: () => -{{{ cDefs.EROFS }}},
      setSize: () => -{{{ cDefs.EROFS }}},
      getSize: (file) => files.get(file)?.size ?? 0,
      read: (file, buffer, length, offset) => Asyncify.handleAsync(async () => {
        const entry = files.get(file);
        if (!entry) return -{{{ cDefs.ENOENT }}};
        if (offset >= entry.size) return 0;
        const data = await Module.frontlineDisc.read(entry.path, offset, Math.min(length, entry.size - offset));
        HEAPU8.set(data, Number(buffer));
        return data.length;
      }),
    };
  },

  frontline_disc_pending__sig: 'vpi',
  frontline_disc_pending: (path, size) => {
    Module.frontlinePending = { path: UTF8ToString(path), size };
  },
});
