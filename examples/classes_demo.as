// Classes Demo
// Demonstrates complete class support in libas3 bytecode VM

// Basic class with constructor
class Point {
    function Point(x, y) {
        this.x = x;
        this.y = y;
    }

    function distance() {
        return this.x * this.x + this.y * this.y;
    }

    function toString() {
        return 'Point(' + this.x + ', ' + this.y + ')';
    }
}

// Create instances
var p1 = new Point(3, 4);
var p2 = new Point(5, 12);

trace('P1:', p1.toString());
trace('P1 distance:', p1.distance());
trace('P2:', p2.toString());
trace('P2 distance:', p2.distance());

// Inheritance example
class Animal {
    function Animal(name) {
        this.name = name;
    }

    function speak() {
        return this.name + ' makes a sound';
    }
}

class Dog extends Animal {
    function Dog(name, breed) {
        this.name = name;
        this.breed = breed;
    }

    function bark() {
        return 'Woof! I am ' + this.name;
    }
}

var dog = new Dog('Rex', 'Labrador');
trace(dog.speak());  // Inherited from Animal
trace(dog.bark());   // Dog's own method
trace('Breed:', dog.breed);

// Class with multiple methods
class Calculator {
    function Calculator() {
        this.result = 0;
    }

    function add(x) {
        this.result = this.result + x;
    }

    function subtract(x) {
        this.result = this.result - x;
    }

    function multiply(x) {
        this.result = this.result * x;
    }

    function getResult() {
        return this.result;
    }
}

var calc = new Calculator();
calc.add(10);
calc.multiply(5);
calc.subtract(3);
trace('Calculator result:', calc.getResult());  // (0 + 10) * 5 - 3 = 47
