// The talk with the dog trader (24.11, his design 17 on the canvas: the paper of the game's own map on the left with
// the dog's papers on it, the trader's line and the answers on the right). Drawing only: stables.cpp keeps the talk's
// state and turns the answers into deeds. Paper and seal: our texture dictionary frontier_dog_ui.ytd (cut from the
// game's map, tools/gen_ytd.py); the dog's picture: the game's satchel drawing of the breed; text: the game's fonts.
#pragma once
#include <string>
#include <vector>

#include "logic/dog.h"

namespace fd::ui {

struct TalkSheet {
    const DogRecord* dog = nullptr;  // nullptr: no paper
    std::string owner;               // «А. Морган» for Arthur's dog, the trader for a dog for sale
    std::string town;                // where the paper is made out
    int value = 0;                   // the trader's price for it, $
    int healthPct = -1;              // live, -1: from the record
    bool noName = false;             // a dog for sale: no name yet
};

struct TalkView {
    TalkSheet sheet;
    std::string line;                  // what the trader says
    std::vector<std::string> options;  // the answers, the last one is «Бывай»/«Назад»
    int selected = 0;
    bool keyboard = true;              // number badges (keyboard) or none (pad)
};

void talkDraw(const TalkView& v);
bool talkTexturesReady();  // our paper streamed (LML); without it a plain paper-coloured sheet is drawn
int talkVisibleRows();      // how many answers fit; the list scrolls past that

#ifdef FD_TEST
void talkFontSurvey();  // test hook: sample lines in the game's font faces
#endif

}  // namespace fd::ui
