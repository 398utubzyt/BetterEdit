#pragma once

#include "KeybindManager.hpp"
#include "KeybindListView.hpp"
#include <BrownAlertDelegate.hpp>
#include "SuperKeyboardManager.hpp"

struct KeybindStoreItem : public CCObject {
    Keybind m_obBind;

    inline KeybindStoreItem(Keybind const& bind) {
        m_obBind = bind;
        this->autorelease();
    }
};

class KeybindEditPopup : public BrownAlertDelegate, public SuperKeyboardDelegate {
    protected:
        KeybindCell* m_pCell;
        KeybindStoreItem* m_pStoreItem;
        Keybind m_obTypedBind;
        CCLabelBMFont* m_pTypeLabel;
        CCLabelBMFont* m_pPreLabel;
        CCLabelBMFont* m_pInfoLabel;

        void setup() override;
        void onRepeat(CCObject*);
        void onRemove(CCObject*);
        void onSet(CCObject*);
        void onClose(CCObject*) override;
        void keyDown(enumKeyCodes) override;
        void keyDownSuper(enumKeyCodes) override;

    public:
        static KeybindEditPopup* create(KeybindCell*, KeybindStoreItem*);
};
