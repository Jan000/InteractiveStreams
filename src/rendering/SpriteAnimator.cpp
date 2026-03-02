#include "rendering/SpriteAnimator.h"
#include <cmath>

namespace is::rendering {

// ── Main draw ────────────────────────────────────────────────────────────────

void SpriteAnimator::draw(sf::RenderTarget& target,
                          sf::Vector2f position,
                          float width, float height,
                          sf::Color color,
                          AnimState state,
                          float timer,
                          int facingDir,
                          sf::Uint8 alpha) {
    color.a = alpha;

    // Squash-and-stretch based on state
    float squash = 1.0f;
    float stretch = 1.0f;
    if (state == AnimState::Jump) {
        stretch = 1.1f;
        squash  = 0.9f;
    } else if (state == AnimState::Dash) {
        squash  = 1.15f;
        stretch = 0.85f;
    } else if (state == AnimState::Attack) {
        float t = std::fmod(timer * 8.0f, 1.0f);
        squash  = 1.0f + 0.05f * std::sin(t * 3.14159f);
        stretch = 1.0f - 0.05f * std::sin(t * 3.14159f);
    }

    float adjW = width  * squash;
    float adjH = height * stretch;

    // Idle bob
    float bob = 0.0f;
    if (state == AnimState::Idle) {
        bob = std::sin(timer * 3.0f) * 1.5f;
    }
    sf::Vector2f adjPos(position.x, position.y + bob);

    // Draw order: legs, body, arms, head, accessories
    drawLegs(target, adjPos, adjW, adjH, color, state, timer, alpha);
    drawBody(target, adjPos, adjW, adjH, color, state, timer, alpha);
    drawArms(target, adjPos, adjW, adjH, color, state, timer, facingDir, alpha);
    drawHead(target, adjPos, adjW, adjH, color, state, timer, facingDir, alpha);
    drawAccessories(target, adjPos, adjW, adjH, color, state, timer, facingDir, alpha);
}

// ── Head ─────────────────────────────────────────────────────────────────────

void SpriteAnimator::drawHead(sf::RenderTarget& target, sf::Vector2f pos,
                               float w, float h, sf::Color color,
                               AnimState state, float timer,
                               int facingDir, sf::Uint8 alpha) {
    float headSize = w * 0.42f;
    float headY = pos.y - h * 0.28f;

    // Head shape (rounded == circle-ish)
    sf::CircleShape head(headSize);
    head.setOrigin(headSize, headSize);
    head.setPosition(pos.x, headY);
    sf::Color headColor = lighten(color, 0.15f);
    headColor.a = alpha;
    head.setFillColor(headColor);
    head.setOutlineThickness(1.5f);
    sf::Color outColor = darken(color, 0.6f);
    outColor.a = alpha;
    head.setOutlineColor(outColor);
    target.draw(head);

    // Eyes
    float eyeR = headSize * 0.22f;
    float eyeOffX = facingDir * headSize * 0.2f;
    float eyeY = headY - headSize * 0.1f;

    // Eye blink animation (blink every ~4 seconds)
    bool blinking = std::fmod(timer, 4.0f) < 0.15f;

    for (int side = -1; side <= 1; side += 2) {
        float ex = pos.x + eyeOffX + side * headSize * 0.32f;

        if (blinking) {
            // Line for closed eye
            sf::RectangleShape lid(sf::Vector2f(eyeR * 2.0f, 1.5f));
            lid.setOrigin(eyeR, 0.75f);
            lid.setPosition(ex, eyeY);
            sf::Color lidColor = darken(color, 0.3f);
            lidColor.a = alpha;
            lid.setFillColor(lidColor);
            target.draw(lid);
        } else {
            // White of eye
            sf::CircleShape eye(eyeR);
            eye.setOrigin(eyeR, eyeR);
            eye.setPosition(ex, eyeY);
            sf::Color eyeColor(255, 255, 255, alpha);
            eye.setFillColor(eyeColor);
            target.draw(eye);

            // Pupil
            float pupilR = eyeR * 0.55f;
            float pupilOffX = facingDir * eyeR * 0.25f;
            float pupilOffY = 0.0f;

            // Look direction based on state
            if (state == AnimState::Jump)  pupilOffY = -eyeR * 0.2f;
            if (state == AnimState::Hit)   pupilOffX = facingDir * -eyeR * 0.3f;

            sf::CircleShape pupil(pupilR);
            pupil.setOrigin(pupilR, pupilR);
            pupil.setPosition(ex + pupilOffX, eyeY + pupilOffY);
            sf::Color pupilColor(20, 20, 30, alpha);
            pupil.setFillColor(pupilColor);
            target.draw(pupil);

            // Highlight
            float hlR = pupilR * 0.35f;
            sf::CircleShape highlight(hlR);
            highlight.setOrigin(hlR, hlR);
            highlight.setPosition(ex + pupilOffX + pupilR * 0.3f,
                                  eyeY + pupilOffY - pupilR * 0.3f);
            sf::Color hlColor(255, 255, 255, static_cast<sf::Uint8>(alpha * 0.8f));
            highlight.setFillColor(hlColor);
            target.draw(highlight);
        }
    }

    // Mouth – small expression
    if (state == AnimState::Attack || state == AnimState::Hit) {
        float mouthW = headSize * 0.5f;
        float mouthH = headSize * 0.18f;
        sf::RectangleShape mouth(sf::Vector2f(mouthW, mouthH));
        mouth.setOrigin(mouthW / 2, mouthH / 2);
        mouth.setPosition(pos.x + eyeOffX, headY + headSize * 0.35f);
        sf::Color mouthColor(40, 10, 10, alpha);
        mouth.setFillColor(mouthColor);
        target.draw(mouth);
    } else if (state == AnimState::Idle || state == AnimState::Walk) {
        // Small smile line
        float smileW = headSize * 0.3f;
        sf::RectangleShape smile(sf::Vector2f(smileW, 1.5f));
        smile.setOrigin(smileW / 2, 0.75f);
        smile.setPosition(pos.x + eyeOffX, headY + headSize * 0.35f);
        sf::Color smileColor = darken(color, 0.4f);
        smileColor.a = alpha;
        smile.setFillColor(smileColor);
        target.draw(smile);
    }
}

// ── Body (torso) ─────────────────────────────────────────────────────────────

void SpriteAnimator::drawBody(sf::RenderTarget& target, sf::Vector2f pos,
                               float w, float h, sf::Color color,
                               AnimState state, float timer, sf::Uint8 alpha) {
    float bodyW = w * 0.55f;
    float bodyH = h * 0.32f;
    float bodyY = pos.y + h * 0.02f;

    // Main torso
    sf::RectangleShape torso(sf::Vector2f(bodyW, bodyH));
    torso.setOrigin(bodyW / 2, bodyH / 2);
    torso.setPosition(pos.x, bodyY);
    sf::Color torsoColor = color;
    torsoColor.a = alpha;
    torso.setFillColor(torsoColor);
    torso.setOutlineThickness(1.0f);
    sf::Color outColor = darken(color, 0.6f);
    outColor.a = alpha;
    torso.setOutlineColor(outColor);
    target.draw(torso);

    // Belt / waist detail
    float beltH = bodyH * 0.12f;
    sf::RectangleShape belt(sf::Vector2f(bodyW * 1.05f, beltH));
    belt.setOrigin(bodyW * 1.05f / 2, beltH / 2);
    belt.setPosition(pos.x, bodyY + bodyH * 0.4f);
    sf::Color beltColor = darken(color, 0.5f);
    beltColor.a = alpha;
    belt.setFillColor(beltColor);
    target.draw(belt);
}

// ── Arms ─────────────────────────────────────────────────────────────────────

void SpriteAnimator::drawArms(sf::RenderTarget& target, sf::Vector2f pos,
                               float w, float h, sf::Color color,
                               AnimState state, float timer,
                               int facingDir, sf::Uint8 alpha) {
    float armW = w * 0.14f;
    float armH = h * 0.28f;
    float bodyW = w * 0.55f;
    float armY = pos.y - h * 0.02f;

    sf::Color armColor = darken(color, 0.8f);
    armColor.a = alpha;
    sf::Color outColor = darken(color, 0.5f);
    outColor.a = alpha;

    for (int side = -1; side <= 1; side += 2) {
        float armX = pos.x + side * (bodyW / 2 + armW * 0.2f);
        float rotation = 0.0f;
        float offY = 0.0f;

        switch (state) {
            case AnimState::Walk: {
                float swing = std::sin(timer * 10.0f + side * 3.14159f) * 25.0f;
                rotation = swing;
                break;
            }
            case AnimState::Attack: {
                // Front arm swings forward, back arm stays
                if (side == facingDir) {
                    float t = std::fmod(timer * 8.0f, 1.0f);
                    rotation = -90.0f * facingDir * std::sin(t * 3.14159f);
                } else {
                    rotation = 10.0f * side;
                }
                break;
            }
            case AnimState::Jump:
                rotation = -30.0f * side;
                offY = -h * 0.03f;
                break;
            case AnimState::Dash:
                rotation = (side == facingDir) ? -40.0f * facingDir : 30.0f * side;
                break;
            case AnimState::Block:
                // Arms in front, guarding
                rotation = -60.0f * side;
                offY = -h * 0.05f;
                break;
            case AnimState::Hit:
                rotation = 15.0f * side;
                offY = h * 0.02f;
                break;
            default:
                // Subtle idle sway
                rotation = std::sin(timer * 2.0f + side * 1.5f) * 5.0f;
                break;
        }

        sf::RectangleShape arm(sf::Vector2f(armW, armH));
        arm.setOrigin(armW / 2, 0);
        arm.setPosition(armX, armY + offY);
        arm.setRotation(rotation);
        arm.setFillColor(armColor);
        arm.setOutlineThickness(1.0f);
        arm.setOutlineColor(outColor);
        target.draw(arm);

        // Hand (small circle at end)
        float handR = armW * 0.6f;
        float handAngle = rotation * 3.14159f / 180.0f;
        float handX = armX + std::sin(handAngle) * armH;
        float handY = armY + offY + std::cos(handAngle) * armH;

        sf::CircleShape hand(handR);
        hand.setOrigin(handR, handR);
        hand.setPosition(handX, handY);
        sf::Color handColor = lighten(color, 0.2f);
        handColor.a = alpha;
        hand.setFillColor(handColor);
        target.draw(hand);
    }
}

// ── Legs ─────────────────────────────────────────────────────────────────────

void SpriteAnimator::drawLegs(sf::RenderTarget& target, sf::Vector2f pos,
                               float w, float h, sf::Color color,
                               AnimState state, float timer, sf::Uint8 alpha) {
    float legW = w * 0.16f;
    float legH = h * 0.3f;
    float legY = pos.y + h * 0.18f;
    float spacing = w * 0.14f;

    sf::Color legColor = darken(color, 0.65f);
    legColor.a = alpha;
    sf::Color outColor = darken(color, 0.4f);
    outColor.a = alpha;

    for (int side = -1; side <= 1; side += 2) {
        float legX = pos.x + side * spacing;
        float rotation = 0.0f;

        switch (state) {
            case AnimState::Walk: {
                float swing = std::sin(timer * 10.0f + side * 3.14159f) * 20.0f;
                rotation = swing;
                break;
            }
            case AnimState::Jump:
                rotation = 10.0f * side;
                break;
            case AnimState::Dash:
                rotation = -15.0f * side;
                break;
            case AnimState::Block:
                rotation = 5.0f * side;
                break;
            default:
                break;
        }

        sf::RectangleShape leg(sf::Vector2f(legW, legH));
        leg.setOrigin(legW / 2, 0);
        leg.setPosition(legX, legY);
        leg.setRotation(rotation);
        leg.setFillColor(legColor);
        leg.setOutlineThickness(1.0f);
        leg.setOutlineColor(outColor);
        target.draw(leg);

        // Shoe (small rectangle at foot)
        float shoeW = legW * 1.3f;
        float shoeH = legW * 0.6f;
        float legAngle = rotation * 3.14159f / 180.0f;
        float footX = legX + std::sin(legAngle) * legH;
        float footY = legY + std::cos(legAngle) * legH;

        sf::RectangleShape shoe(sf::Vector2f(shoeW, shoeH));
        shoe.setOrigin(shoeW / 2, 0);
        shoe.setPosition(footX, footY);
        shoe.setRotation(rotation);
        sf::Color shoeColor = darken(color, 0.35f);
        shoeColor.a = alpha;
        shoe.setFillColor(shoeColor);
        target.draw(shoe);
    }
}

// ── Accessories ──────────────────────────────────────────────────────────────

void SpriteAnimator::drawAccessories(sf::RenderTarget& target, sf::Vector2f pos,
                                      float w, float h, sf::Color color,
                                      AnimState state, float timer,
                                      int facingDir, sf::Uint8 alpha) {
    switch (state) {
        case AnimState::Attack: {
            // Sword swing effect
            float t = std::fmod(timer * 8.0f, 1.0f);
            float angle = -80.0f + 160.0f * t;
            float swordLen = h * 0.5f;
            float swordW = w * 0.08f;
            float armX = pos.x + facingDir * (w * 0.4f);
            float armY = pos.y - h * 0.08f;

            sf::RectangleShape sword(sf::Vector2f(swordW, swordLen));
            sword.setOrigin(swordW / 2, 0);
            sword.setPosition(armX, armY);
            sword.setRotation(angle * facingDir);
            sf::Color swordColor(220, 230, 240, alpha);
            sword.setFillColor(swordColor);
            sword.setOutlineThickness(1.0f);
            sf::Color swordOutline(180, 190, 200, alpha);
            sword.setOutlineColor(swordOutline);
            target.draw(sword);

            // Sword tip glow
            float tipAngle = angle * facingDir * 3.14159f / 180.0f;
            float tipX = armX + std::sin(tipAngle) * swordLen;
            float tipY = armY + std::cos(tipAngle) * swordLen;
            float glowR = w * 0.12f;
            sf::CircleShape glow(glowR);
            glow.setOrigin(glowR, glowR);
            glow.setPosition(tipX, tipY);
            sf::Color glowColor(255, 255, 200, static_cast<sf::Uint8>(alpha * 0.4f));
            glow.setFillColor(glowColor);
            target.draw(glow);
            break;
        }

        case AnimState::Block: {
            // Shield
            float shieldX = pos.x + facingDir * w * 0.35f;
            float shieldY = pos.y - h * 0.05f;
            float shieldR = w * 0.35f;

            sf::CircleShape shield(shieldR);
            shield.setOrigin(shieldR, shieldR);
            shield.setPosition(shieldX, shieldY);
            sf::Color shieldColor(100, 200, 255, static_cast<sf::Uint8>(alpha * 0.5f));
            shield.setFillColor(shieldColor);
            shield.setOutlineThickness(2.0f);
            sf::Color shieldOutline(150, 220, 255, alpha);
            shield.setOutlineColor(shieldOutline);
            target.draw(shield);
            break;
        }

        case AnimState::Dash: {
            // Speed lines behind character
            sf::Color lineColor = lighten(color, 0.3f);
            lineColor.a = static_cast<sf::Uint8>(alpha * 0.5f);
            for (int i = 0; i < 4; ++i) {
                float ly = pos.y - h * 0.2f + i * h * 0.13f;
                float lx = pos.x - facingDir * w * 0.5f;
                float lineLen = w * (0.5f + i * 0.15f);

                sf::RectangleShape line(sf::Vector2f(lineLen, 2.0f));
                line.setOrigin(0, 1.0f);
                line.setPosition(lx - facingDir * lineLen, ly);
                line.setFillColor(lineColor);
                target.draw(line);
            }
            break;
        }

        case AnimState::Hit: {
            // Impact star effect
            float starSize = w * 0.2f;
            float hitX = pos.x - facingDir * w * 0.3f;
            float hitY = pos.y - h * 0.1f;
            float flash = std::fmod(timer * 15.0f, 1.0f);

            if (flash < 0.5f) {
                for (int i = 0; i < 4; ++i) {
                    float angle = i * 45.0f + timer * 200.0f;
                    sf::RectangleShape ray(sf::Vector2f(2.0f, starSize));
                    ray.setOrigin(1.0f, starSize / 2);
                    ray.setPosition(hitX, hitY);
                    ray.setRotation(angle);
                    sf::Color rayColor(255, 255, 100, static_cast<sf::Uint8>(alpha * 0.7f));
                    ray.setFillColor(rayColor);
                    target.draw(ray);
                }
            }
            break;
        }

        default:
            break;
    }
}

} // namespace is::rendering
