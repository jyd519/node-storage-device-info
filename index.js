const storage = require ("./build/Release/storage");

exports.getPartitionSpace = function(path, cb) {
	try {
		storage.getPartitionSpace(path, cb);
	} catch(error) {
		cb(error);
	}
};
