## Description

This is a hack for the [Crunchlabs Hack Pack Box 5 - Laser Tag](https://www.crunchlabs.com/products/laser). This hack adds an OLED screen that displays information for a custom game mode, **Ultra Beam**.

<p float="left">
  <img src="images/screen.jpg" alt="Example Image 1" height="400" />
  <img src="images/mount.jpg" alt="Example Image 2" height="400" />
</p>

## Game Mechanics

* **Lives** - Each player starts with **2 lives**. 
* **Game Over** - Players reduced to 0 lives are out of the game with a **Game Over** screen.
* **3 Health per life** – The goggles dim by 1/3 per health lost
* **6 Max Ammo** – Reload 1 ammo per second while holding the reload button
* **Ultra Beam:**
  * Charge the **Ultra Beam** by holding reload for 5 seconds when your ammo is maxed out
  * Fire a devastating shot that reduces your target’s health to 0, but at a cost:
    * Lose 1 health
    * Consume all your ammo
    * Only 1 Ultra Beam per game
  * Make it count!

## OLED Screen Features

Displays critical game info, including:
* Ammo count
* Team number
* Lives
* Which team hit you
* Whether you were hit by an **Ultra Beam**

## Hardware

* [Crunchlabs Hack Pack Box 5 - Laser Tag](https://www.crunchlabs.com/products/laser)
* 2 [0.96 Inch OLED I2C Display Module](https://www.amazon.com/gp/product/B09C5K91H7)
* A 3D printer or access to one to print the screen mount. The screen can also be held on by strapping the wires down with the velcro battery strap.
* 2 [Push Buttons](https://www.amazon.com/gp/product/B07ZV3PB26) for reload. - This is what I used but something with more of a click would be better.
* 2 [3v Mini Vibration Motors](https://www.amazon.com/gp/product/B0B4SK8M1C)
* A Hot glue gun - The vibration motor adhesive did not hold.
* [Dupont Wires](https://www.amazon.com/gp/product/B07GD2BWPY)
* A soldering iron
* 8 M2 screws - 4mm or longer.

## Wiring

OLED:
* SDA -> A4
* SCL -> A5
* VCC -> 5v Rail
* GND -> GND Rail
Reload Button
* Positive Terminal -> D10
* Negative Terminal -> GND Rail
Vibration Motors
* Positive Wire -> D11
* Negative Wire -> GND Rail