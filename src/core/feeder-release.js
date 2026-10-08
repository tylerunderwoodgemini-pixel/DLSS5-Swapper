'use strict';

// One verified release for the shader, both client architectures and helper.
// Never mix host protocol versions. Digest supplied by GitHub's release API.
module.exports = {
  version: '0.12.0',
  archive: ['DLSS5-Feeder-0.12.0.zip', 'https://github.com/jlrouzies-fr/DLSS5-Feeder/releases/download/v0.12.0/DLSS5-Feeder-0.12.0.zip', 'e970537996f6e73dce9a510b9e015fad19f148ee736dc4f518ccdebf6f012558'],
  hashes: {
    'dlss5-feed.addon32': 'd2df9fbf9b5e0cc24291b9240e4f8dd2aae063592571bbed37302878b6dac74c',
    'dlss5-feed.addon64': 'd890b37ff5943cc82ea1f305c56f618725ac929c6edf886519fe32b7fba5b56c',
    'dlss5-feed-host64.exe': '397dbf49c3a2b5f3bc13cfa0e0b3df4316edae9c45be7379bcacad066ebb07f2',
    'reshade-shaders/Shaders/DLSS5_Feed.fx': '955d911d3b567c57f4e0b44e528dae3f3df286fd8fd3e775b9f6b5ddd561aa94'
  }
};
