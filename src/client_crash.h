
int should_crash(int num);

enum crash_num {
  CRASH_FETCH = 1,
  CRASH_STORE = 2,
  CRASH_OPEN = 3,
  CRASH_RELEASE_START = 4,
  CRASH_RELEASE_AFTER_STORE = 5,
  CRASH_TRUNCATE = 6,
  CRASH_WRITE = 7,
  CRASH_MKDIR = 8,
  CRASH_RMDIR = 9,
  CRASH_CREATE_AFTER_REMOTE = 10,
  CRASH_UNLINK = 11,
};

void check_crash(int num);
