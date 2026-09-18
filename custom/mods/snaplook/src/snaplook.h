class Snaplook {
private:
    bool inSnaplook = false;

public:
    int getPerspectiveOption(void* t);
    void onMouseLocked();
    void onKeyboard(int keyCode, int action);
};

// Call once after all mods are loaded (window creation) to resolve the optional
// zoom-integration symbol.
void snaplookResolveZoomLink();