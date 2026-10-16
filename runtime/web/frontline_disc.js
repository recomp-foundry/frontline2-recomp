// Where the web build's disc comes from (--pre-js). The page sets
// Module.frontlineDisc before the program starts; without it, the disc is the
// extracted directory serve-web.py offers at disc/ (development).
//   list()                  -> "<size>\t<relative path>\n" for every file
//   read(path, offset, len) -> Promise<Uint8Array>
Module.frontlineDisc ??= {
  async list() {
    const response = await fetch('disc/.frontline-manifest');
    if (!response.ok) throw new Error('no disc at disc/ (serve-web.py --disc-dir)');
    return response.text();
  },
  async read(path, offset, length) {
    const response = await fetch('disc/' + path, { headers: { Range: `bytes=${offset}-${offset + length - 1}` } });
    return new Uint8Array(await response.arrayBuffer());
  },
};

