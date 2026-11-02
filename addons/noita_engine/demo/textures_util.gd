class_name TexturesUtil

# Generated demo textures, kept out of the demos that need them.


static func box(w: int, h: int, colour: Color) -> ImageTexture:
	var image := Image.create(w, h, false, Image.FORMAT_RGBA8)
	image.fill(colour)
	for x: int in w:
		image.set_pixel(x, 0, colour.darkened(0.3))
		image.set_pixel(x, h - 1, colour.darkened(0.3))
	for y: int in h:
		image.set_pixel(0, y, colour.darkened(0.3))
		image.set_pixel(w - 1, y, colour.darkened(0.3))
	return ImageTexture.create_from_image(image)


static func disc(diameter: int, colour: Color) -> ImageTexture:
	var image := Image.create(diameter, diameter, false, Image.FORMAT_RGBA8)
	var r := diameter / 2.0
	for y: int in diameter:
		for x: int in diameter:
			var dx := x + 0.5 - r
			var dy := y + 0.5 - r
			image.set_pixel(x, y, colour.lightened(0.15 * float(x < r)) if dx * dx + dy * dy <= r * r else Color(0, 0, 0, 0))
	return ImageTexture.create_from_image(image)
